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
    std::size_t memoryBudget = std::size_t{1536} << 20; ///< Bytes of loaded tiles (+ pyramids).
    /// Samples finer than this (metres between mesh vertices) REQUEST the tile they fall in.
    double requestSpacing = 400.0;
    /// Samples coarser than this never use tiles at all (blending in between). So coarse LOD
    /// levels - most of the planet - do not depend on what happens to be loaded, and never
    /// need rebuilding when tiles come and go.
    double fadeSpacing = 800.0;
    /// A tile used during the last `keepTicks` updates is never evicted (no thrashing).
    std::uint64_t keepTicks = 120;
    /// Requests not renewed for this many updates are dropped (the camera has moved on).
    std::uint64_t requestLifetime = 300;
};

/// High-resolution .hgt tiles, loaded ON DEMAND and evicted under a memory budget: whole-planet
/// coverage with only the tiles around the camera in memory.
///
/// Demand comes from the terrain itself: when a mesh worker samples a point at fine spacing
/// inside a tile that exists on disk but is not loaded, sample() records a request (one relaxed
/// atomic store) and returns "no data" - the mesh uses the base terrain for now. A loader thread
/// reads requested tiles, most recent first, builds their mip pyramids and PUBLISHES them: an
/// immutable snapshot of the loaded set is swapped in, so readers never lock and never see a
/// half-built tile. Every publish increases version(); update() reports which tiles changed,
/// and the renderer rebuilds the chunks over them.
///
/// Blending: like the static mosaic, data fades into the base over kHighResolutionFeather - but
/// towards every NEIGHBOUR square that is not loaded (or does not exist), towards voids, and
/// with sample spacing (see TileStreamingSettings::fadeSpacing). Tiles may mix 3" and 1".
///
/// Thread safety: sample() and version() from any thread; update(), loadNow(), idle() and
/// stats() from one controlling thread.
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
        int lat = 0; ///< South-west corner of the tile, degrees.
        int lon = 0;
        bool loaded = false; ///< false = evicted
        std::uint64_t version = 0;
    };
    /// Once per frame: advances the clock, evicts least recently used tiles above the budget and
    /// returns the tiles loaded or evicted since the last call.
    std::vector<Change> update();

    /// Loads a tile synchronously (tools, tests). False if there is no such tile on disk.
    bool loadNow(int lat, int lon);

    /// Nothing is waiting to be loaded (that fits the budget) and the loader is not busy.
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
        std::atomic<std::uint64_t> requested{0}; ///< Clock tick of the last request.
        std::atomic<std::uint64_t> used{0};      ///< Clock tick of the last sample.
        std::atomic<std::uint64_t> attempted{0}; ///< Clock tick of the last load attempt.
        std::atomic<bool> failed{false};
        std::atomic<bool> inFlight{false}; ///< Being loaded right now.
    };

    static int key(int lat, int lon);
    /// The current snapshot, cached per thread and refreshed only when version() changes - so
    /// the hot path costs one atomic load, not a lock or a contended reference count.
    const Snapshot& currentSnapshot() const;
    /// Requested recently, not attempted since, not being loaded, not loaded in `loaded`.
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
    const std::uint64_t instance_;               ///< Unique id, for the per-thread snapshot cache.

    std::atomic<std::uint64_t> clock_{1};
    std::atomic<std::uint64_t> version_{0};

    mutable std::mutex mutex_; ///< Guards snapshot_, changes_, bytes_, stats and busy_.
    std::condition_variable_any wake_;
    std::shared_ptr<const Snapshot> snapshot_;
    std::vector<Change> changes_;
    std::size_t bytes_ = 0;
    std::uint64_t loads_ = 0;
    std::uint64_t evictions_ = 0;
    bool busy_ = false;

    std::jthread loader_; ///< Last: stops and joins before anything it uses is destroyed.
};

} // namespace orbis
