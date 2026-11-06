#pragma once

#include "core/terrain/HgtTiles.hpp"
#include "core/terrain/HighResolutionTerrain.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

namespace orbis {

struct TileStreamingSettings {
    std::size_t memoryBudget = std::size_t{1536} << 20; ///< Bytes, pyramids included.
    /// Samples finer than this (metres) request their tile.
    double requestSpacing = 400.0;
    /// Samples coarser than this ignore tiles, so coarse LOD never depends on what is loaded.
    double fadeSpacing = 800.0;
    /// Tiles used within this many updates are not evicted.
    std::uint64_t keepTicks = 120;
    /// Requests not renewed for this many updates expire.
    std::uint64_t requestLifetime = 300;
};

/// .hgt tiles loaded on demand and evicted under a memory budget.
///
/// A fine sample inside an unloaded tile records a request (a relaxed atomic store) and returns
/// no data. The loader thread loads the most recent request and publishes an immutable snapshot
/// of the loaded set, bumping version(); readers never lock. update() reports what changed so
/// the renderer can rebuild the chunks there. Data fades out towards unloaded neighbours, voids
/// and coarse sample spacings.
///
/// sample() and version() may be called from any thread, the rest from one controlling thread.
class HgtTileStreamer final : public HighResolutionTerrain {
public:
    HgtTileStreamer(std::vector<HgtTileFile> files, const TileStreamingSettings& settings = {});
    ~HgtTileStreamer() override;

    HgtTileStreamer(const HgtTileStreamer&) = delete;
    HgtTileStreamer& operator=(const HgtTileStreamer&) = delete;

    std::optional<Sample> sample(double latitude, double longitude,
                                 double sampleSpacing) const override;
    std::uint64_t version() const override { return version_.load(std::memory_order_acquire); }

    struct Change {
        int lat = 0; ///< South-west corner, degrees.
        int lon = 0;
        bool loaded = false; ///< or evicted
        std::uint64_t version = 0;
    };
    /// Once per frame: advances the clock, evicts LRU tiles as needed, returns what changed.
    std::vector<Change> update();

    /// Synchronous load. False if the tile is not on disk.
    bool loadNow(int lat, int lon);

    /// Nothing loadable is pending and the loader is idle.
    bool idle() const;

    struct Stats {
        std::size_t available = 0;
        std::size_t loaded = 0;
        std::size_t bytes = 0;
        std::size_t pending = 0;
        std::uint64_t loads = 0;
        std::uint64_t evictions = 0;
    };
    Stats stats() const;

private:
    struct Tile {
        ElevationGrid grid;
        std::optional<ElevationGrid> coverage; ///< Only for tiles with voids.
        double spacing = 0.0;
        std::size_t bytes = 0;
    };
    using TileMap = std::unordered_map<int, std::shared_ptr<const Tile>>;
    struct Snapshot {
        TileMap tiles;
        std::uint64_t version = 0;
    };
    struct Entry {
        HgtTileFile file;
        // Clock ticks.
        std::atomic<std::uint64_t> requested{0};
        std::atomic<std::uint64_t> used{0};
        std::atomic<std::uint64_t> attempted{0};
        std::atomic<bool> failed{false};
        std::atomic<bool> inFlight{false};
    };

    static int key(int lat, int lon);
    /// Cached per thread and refreshed when version() changes: no lock or contended refcount on
    /// the hot path.
    const Snapshot& currentSnapshot() const;
    /// Requested recently, not attempted since, and neither loading nor loaded.
    bool isPending(const Entry& e, std::uint64_t now, const TileMap& loaded) const;
    std::size_t estimateBytes(const Entry& e) const;
    void publish(int k, std::shared_ptr<const Tile> tile, bool loaded, int lat, int lon);
    std::shared_ptr<const Tile> load(const HgtTileFile& file) const;
    Entry* pickRequest();
    void loaderLoop(std::stop_token stop);

    TileStreamingSettings settings_;
    std::unique_ptr<Entry[]> entries_;
    std::size_t entryCount_ = 0;
    std::unordered_map<int, std::size_t> index_; ///< Immutable after construction.
    const std::uint64_t instance_;               ///< Keys the per-thread snapshot cache.

    std::atomic<std::uint64_t> clock_{1};
    std::atomic<std::uint64_t> version_{0};

    mutable std::mutex mutex_; ///< Guards everything below except the thread.
    std::condition_variable_any wake_;
    std::shared_ptr<const Snapshot> snapshot_;
    std::vector<Change> changes_;
    std::size_t bytes_ = 0;
    std::uint64_t loads_ = 0;
    std::uint64_t evictions_ = 0;
    bool busy_ = false;

    std::jthread loader_; ///< Last: joins before anything it uses is destroyed.
};

} // namespace orbis
