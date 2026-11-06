#include "core/terrain/HgtTileStreamer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <numbers>

namespace orbis {

namespace {

constexpr double kMeanRadius = 6371000.0;
constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kMetresPerDegree = kMeanRadius * kDeg;

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

std::atomic<std::uint64_t> nextInstance{1};

std::size_t gridBytes(int n) {
    const auto nodes = static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    return nodes * sizeof(float) * 4 / 3;
}

} // namespace

int HgtTileStreamer::key(int lat, int lon) {
    lon = ((lon + 180) % 360 + 360) % 360 - 180; // wrap across the date line
    return (lat + 90) * 360 + (lon + 180);
}

HgtTileStreamer::HgtTileStreamer(std::vector<HgtTileFile> files, const TileStreamingSettings& settings)
    : settings_(settings), entries_(std::make_unique<Entry[]>(files.size())), entryCount_(files.size()),
      instance_(nextInstance.fetch_add(1)), snapshot_(std::make_shared<const Snapshot>()) {
    for (std::size_t i = 0; i < files.size(); ++i) {
        index_.emplace(key(files[i].lat, files[i].lon), i);
        entries_[i].file = std::move(files[i]);
    }
    loader_ = std::jthread([this](std::stop_token stop) { loaderLoop(stop); });
}

HgtTileStreamer::~HgtTileStreamer() {
    loader_.request_stop();
    wake_.notify_all();
}

const HgtTileStreamer::Snapshot& HgtTileStreamer::currentSnapshot() const {
    struct Cache {
        std::uint64_t instance = 0;
        std::uint64_t version = ~std::uint64_t{0};
        std::shared_ptr<const Snapshot> snapshot;
    };
    thread_local Cache cache;
    const std::uint64_t v = version_.load(std::memory_order_acquire);
    if (cache.instance != instance_ || cache.version != v || !cache.snapshot) {
        const std::lock_guard lock(mutex_);
        cache.snapshot = snapshot_;
        cache.version = snapshot_->version; // may be newer than v
        cache.instance = instance_;
    }
    return *cache.snapshot;
}

std::optional<HighResolutionTerrain::Sample>
HgtTileStreamer::sample(double latitude, double longitude, double sampleSpacing) const {
    const double spacingWeight =
        sampleSpacing > 0.0 ? 1.0 - smoothstep(settings_.requestSpacing, settings_.fadeSpacing, sampleSpacing)
                            : 1.0;
    if (spacingWeight <= 0.0) {
        return std::nullopt;
    }
    const double latDeg = latitude / kDeg;
    const double lonDeg = longitude / kDeg;
    const int lat = static_cast<int>(std::floor(latDeg));
    const int lon = static_cast<int>(std::floor(lonDeg));
    const auto entry = index_.find(key(lat, lon));
    if (entry == index_.end()) {
        return std::nullopt;
    }
    Entry& e = entries_[entry->second];
    const std::uint64_t now = clock_.load(std::memory_order_relaxed);

    const Snapshot& snapshot = currentSnapshot();
    const auto it = snapshot.tiles.find(entry->first);
    if (it == snapshot.tiles.end()) {
        if (sampleSpacing < settings_.requestSpacing &&
            e.requested.load(std::memory_order_relaxed) != now) {
            e.requested.store(now, std::memory_order_relaxed);
        }
        return std::nullopt;
    }
    // Store only on change, to avoid cache-line ping-pong between workers.
    if (e.used.load(std::memory_order_relaxed) != now) {
        e.used.store(now, std::memory_order_relaxed);
    }

    const Tile& tile = *it->second;
    const double level =
        sampleSpacing > 0.0 ? std::max(std::log2(sampleSpacing / tile.spacing), 0.0) : 0.0;
    const double height = tile.grid.sample(latitude, longitude, level);
    if (std::isnan(height)) {
        return std::nullopt;
    }
    double weight = spacingWeight;
    if (tile.coverage) {
        weight *= smoothstep(0.55, 0.95,
                             tile.coverage->sample(latitude, longitude,
                                                   static_cast<double>(tile.coverage->levelCount() - 1)));
    }
    // Fade towards every neighbouring square without loaded data.
    double nearestMissing = 1e9;
    const double cosLat = std::cos(latitude);
    for (int dLat = -1; dLat <= 1; ++dLat) {
        for (int dLon = -1; dLon <= 1; ++dLon) {
            if (dLat == 0 && dLon == 0) {
                continue;
            }
            const int nLat = lat + dLat;
            const int nLon = lon + dLon;
            if (nLat >= -90 && nLat < 90 && snapshot.tiles.contains(key(nLat, nLon))) {
                continue;
            }
            const double dy = std::max({nLat - latDeg, 0.0, latDeg - (nLat + 1)});
            const double dx = std::max({nLon - lonDeg, 0.0, lonDeg - (nLon + 1)}) * cosLat;
            nearestMissing = std::min(nearestMissing, std::sqrt(dx * dx + dy * dy) * kMetresPerDegree);
        }
    }
    weight *= smoothstep(0.0, kHighResolutionFeather, nearestMissing);
    return Sample{height, weight, tile.spacing};
}

std::size_t HgtTileStreamer::estimateBytes(const Entry& e) const { return gridBytes(e.file.samples); }

bool HgtTileStreamer::isPending(const Entry& e, std::uint64_t now, const TileMap& loaded) const {
    const std::uint64_t requested = e.requested.load(std::memory_order_relaxed);
    return requested != 0 && requested > e.attempted.load(std::memory_order_relaxed) &&
           requested + settings_.requestLifetime >= now && !e.failed.load(std::memory_order_relaxed) &&
           !e.inFlight.load(std::memory_order_acquire) && !loaded.contains(key(e.file.lat, e.file.lon));
}

std::shared_ptr<const HgtTileStreamer::Tile> HgtTileStreamer::load(const HgtTileFile& file) const {
    HgtTile loaded = loadHgtTile(file);
    const double spacing = loaded.grid.nodeSpacing(0, kMeanRadius);
    std::optional<ElevationGrid> coverage;
    std::size_t bytes = gridBytes(file.samples);
    if (loaded.hasVoids) {
        coverage = loaded.grid.validityMask();
        bytes *= 2;
    }
    return std::make_shared<const Tile>(Tile{std::move(loaded.grid), std::move(coverage), spacing, bytes});
}

void HgtTileStreamer::publish(int k, std::shared_ptr<const Tile> tile, bool loaded, int lat, int lon) {
    // Caller holds mutex_. Copy-on-write: snapshots held by readers are never modified.
    auto next = std::make_shared<Snapshot>(*snapshot_);
    if (loaded) {
        if (const auto old = next->tiles.find(k); old != next->tiles.end()) {
            bytes_ -= old->second->bytes;
        }
        bytes_ += tile->bytes;
        next->tiles[k] = std::move(tile);
        ++loads_;
    } else if (const auto old = next->tiles.find(k); old != next->tiles.end()) {
        bytes_ -= old->second->bytes;
        next->tiles.erase(old);
        ++evictions_;
    }
    next->version = snapshot_->version + 1;
    snapshot_ = std::move(next);
    version_.store(snapshot_->version, std::memory_order_release);
    changes_.push_back({lat, lon, loaded, snapshot_->version});
}

HgtTileStreamer::Entry* HgtTileStreamer::pickRequest() {
    // The most recent request is where the camera is now.
    const std::uint64_t now = clock_.load(std::memory_order_relaxed);
    Entry* best = nullptr;
    std::uint64_t bestTick = 0;
    std::size_t bytes = 0;
    std::shared_ptr<const Snapshot> snapshot;
    {
        const std::lock_guard lock(mutex_);
        bytes = bytes_;
        snapshot = snapshot_;
    }
    for (std::size_t i = 0; i < entryCount_; ++i) {
        Entry& e = entries_[i];
        const std::uint64_t requested = e.requested.load(std::memory_order_relaxed);
        if (isPending(e, now, snapshot->tiles) && requested > bestTick &&
            bytes + estimateBytes(e) <= settings_.memoryBudget) {
            best = &e;
            bestTick = requested;
        }
    }
    return best;
}

void HgtTileStreamer::loaderLoop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        Entry* e = pickRequest();
        if (e == nullptr) {
            std::unique_lock lock(mutex_);
            // Polling, so that requests stay a plain atomic store on the sampling side.
            wake_.wait_for(lock, stop, std::chrono::milliseconds(5), [] { return false; });
            continue;
        }
        {
            const std::lock_guard lock(mutex_);
            busy_ = true;
            e->inFlight.store(true, std::memory_order_release);
        }
        e->attempted.store(e->requested.load(std::memory_order_relaxed), std::memory_order_relaxed);
        std::shared_ptr<const Tile> tile;
        try {
            tile = load(e->file);
        } catch (const std::exception& ex) {
            std::cerr << "Warning: " << ex.what() << " (tile skipped)\n";
            e->failed.store(true, std::memory_order_relaxed);
        }
        const std::lock_guard lock(mutex_);
        if (tile) {
            publish(key(e->file.lat, e->file.lon), std::move(tile), true, e->file.lat, e->file.lon);
        }
        // Requests made while loading are satisfied too, or an eviction would bring it back.
        e->attempted.store(clock_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        e->inFlight.store(false, std::memory_order_release);
        busy_ = false;
    }
}

std::vector<HgtTileStreamer::Change> HgtTileStreamer::update() {
    const std::uint64_t now = clock_.fetch_add(1, std::memory_order_relaxed) + 1;

    // Make room for the most wanted pending tile, or a full budget would never admit one.
    const std::lock_guard lock(mutex_);
    std::size_t need = 0;
    std::uint64_t bestTick = 0;
    for (std::size_t i = 0; i < entryCount_; ++i) {
        const Entry& e = entries_[i];
        const std::uint64_t requested = e.requested.load(std::memory_order_relaxed);
        if (isPending(e, now, snapshot_->tiles) && requested > bestTick) {
            bestTick = requested;
            need = estimateBytes(e);
        }
    }

    if (bytes_ + need > settings_.memoryBudget) {
        std::vector<std::pair<std::uint64_t, int>> candidates;
        for (const auto& [k, tile] : snapshot_->tiles) {
            const std::uint64_t used = entries_[index_.at(k)].used.load(std::memory_order_relaxed);
            if (used + settings_.keepTicks < now) {
                candidates.emplace_back(used, k);
            }
        }
        std::sort(candidates.begin(), candidates.end());
        for (const auto& [used, k] : candidates) {
            if (bytes_ + need <= settings_.memoryBudget) {
                break;
            }
            const HgtTileFile& f = entries_[index_.at(k)].file;
            publish(k, nullptr, false, f.lat, f.lon);
        }
    }
    return std::exchange(changes_, {});
}

bool HgtTileStreamer::loadNow(int lat, int lon) {
    const auto it = index_.find(key(lat, lon));
    if (it == index_.end()) {
        return false;
    }
    Entry& e = entries_[it->second];
    {
        const std::lock_guard lock(mutex_);
        if (snapshot_->tiles.contains(it->first)) {
            return true;
        }
    }
    auto tile = load(e.file);
    e.attempted.store(clock_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    const std::lock_guard lock(mutex_);
    publish(it->first, std::move(tile), true, lat, lon);
    return true;
}

bool HgtTileStreamer::idle() const {
    const std::uint64_t now = clock_.load(std::memory_order_relaxed);
    const std::lock_guard lock(mutex_);
    if (busy_) {
        return false;
    }
    for (std::size_t i = 0; i < entryCount_; ++i) {
        const Entry& e = entries_[i];
        if (isPending(e, now, snapshot_->tiles) && bytes_ + estimateBytes(e) <= settings_.memoryBudget) {
            return false;
        }
    }
    return true;
}

HgtTileStreamer::Stats HgtTileStreamer::stats() const {
    const std::uint64_t now = clock_.load(std::memory_order_relaxed);
    const std::lock_guard lock(mutex_);
    Stats s;
    s.available = entryCount_;
    s.loaded = snapshot_->tiles.size();
    s.bytes = bytes_;
    s.loads = loads_;
    s.evictions = evictions_;
    for (std::size_t i = 0; i < entryCount_; ++i) {
        const Entry& e = entries_[i];
        if (isPending(e, now, snapshot_->tiles)) {
            ++s.pending;
        }
    }
    return s;
}

} // namespace orbis
