#include "core/geo/Ellipsoid.hpp"
#include "core/terrain/EarthTerrain.hpp"
#include "core/terrain/HgtTileStreamer.hpp"
#include "hgt_test_tiles.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using namespace orbis;
using Catch::Matchers::WithinAbs;
using orbis::test::freshDir;
using orbis::test::writeTile;

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
namespace fs = std::filesystem;

double plane(double lat, double lon) { return 1000.0 * (lat - 45.0) + 200.0 * (lon - 7.0) + 100.0; }

/// Drives the streamer like the render loop does, until it has nothing left to do.
std::vector<HgtTileStreamer::Change> settle(HgtTileStreamer& s) {
    std::vector<HgtTileStreamer::Change> all;
    const auto start = std::chrono::steady_clock::now();
    do {
        for (const auto& c : s.update()) {
            all.push_back(c);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (!s.idle() && std::chrono::steady_clock::now() - start < std::chrono::seconds(20));
    for (const auto& c : s.update()) {
        all.push_back(c);
    }
    return all;
}

std::optional<HighResolutionTerrain::Sample> at(const HgtTileStreamer& s, double lat, double lon,
                                                double spacing = 30.0) {
    return s.sample(lat * kDeg, lon * kDeg, spacing);
}
} // namespace

TEST_CASE("streamer: tiles load on demand and are then used", "[hgt][streaming]") {
    const fs::path dir = freshDir("orbis_stream_demand");
    writeTile(dir, 45, 7, 121, plane);
    writeTile(dir, 45, 8, 121, plane);
    HgtTileStreamer s(scanHgtDirectory(dir));
    CHECK(s.stats().available == 2);
    CHECK(s.stats().loaded == 0);
    CHECK(s.version() == 0);

    // First fine sample: no data yet, but a request.
    CHECK_FALSE(at(s, 45.5, 7.5).has_value());
    const auto changes = settle(s);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].lat == 45);
    CHECK(changes[0].lon == 7);
    CHECK(changes[0].loaded);
    CHECK(changes[0].version == s.version());
    CHECK(s.stats().loaded == 1); // only what was asked for

    // Now the real data; at the border with the NOT loaded neighbour it fades out.
    const auto inside = at(s, 45.5, 7.5);
    REQUIRE(inside.has_value());
    CHECK_THAT(inside->height, WithinAbs(plane(45.5, 7.5), 2.0));
    CHECK(inside->weight == 1.0);
    const auto nearEast = at(s, 45.5, 7.999);
    REQUIRE(nearEast.has_value());
    CHECK(nearEast->weight < 0.1);
    fs::remove_all(dir);
}

TEST_CASE("streamer: coarse samples neither request nor use tiles", "[hgt][streaming]") {
    const fs::path dir = freshDir("orbis_stream_coarse");
    writeTile(dir, 45, 7, 121, plane);
    TileStreamingSettings settings;
    HgtTileStreamer s(scanHgtDirectory(dir), settings);
    CHECK_FALSE(at(s, 45.5, 7.5, 5000.0).has_value());
    settle(s);
    CHECK(s.stats().loaded == 0);

    REQUIRE(s.loadNow(45, 7));
    CHECK_FALSE(at(s, 45.5, 7.5, settings.fadeSpacing * 1.01).has_value()); // ignored when coarse
    const auto mid = at(s, 45.5, 7.5, 0.5 * (settings.requestSpacing + settings.fadeSpacing));
    REQUIRE(mid.has_value());
    CHECK(mid->weight > 0.3);
    CHECK(mid->weight < 0.7);
    CHECK_FALSE(s.loadNow(10, 10)); // not on disk
    fs::remove_all(dir);
}

TEST_CASE("streamer: neighbouring loaded tiles join without a seam", "[hgt][streaming]") {
    const fs::path dir = freshDir("orbis_stream_seam");
    writeTile(dir, 45, 7, 121, plane);
    writeTile(dir, 45, 8, 121, plane);
    writeTile(dir, 46, 7, 121, plane);
    writeTile(dir, 46, 8, 121, plane);
    HgtTileStreamer s(scanHgtDirectory(dir));
    for (int lat : {45, 46}) {
        for (int lon : {7, 8}) {
            REQUIRE(s.loadNow(lat, lon));
        }
    }
    // Across the shared corner at 46 N 8 E, every weight is 1 and the heights are continuous.
    for (const auto& [lat, lon] : {std::pair{45.999999, 7.999999}, {46.000001, 8.000001},
                                   {45.999999, 8.000001}, {46.000001, 7.999999}}) {
        const auto v = at(s, lat, lon);
        REQUIRE(v.has_value());
        CHECK(v->weight == 1.0);
        CHECK_THAT(v->height, WithinAbs(plane(46.0, 8.0), 1.0));
    }
    fs::remove_all(dir);
}

TEST_CASE("streamer: least recently used tiles are evicted to fit the budget", "[hgt][streaming]") {
    const fs::path dir = freshDir("orbis_stream_evict");
    for (int lon = 0; lon < 4; ++lon) {
        writeTile(dir, 10, lon, 241, [](double, double) { return 100.0; });
    }
    TileStreamingSettings settings;
    settings.keepTicks = 3;
    settings.memoryBudget = 2 * (241 * 241 * 4 * 4 / 3) + 1000; // room for two tiles
    HgtTileStreamer s(scanHgtDirectory(dir), settings);

    // Visit the tiles one after the other, like a camera flying east.
    for (int lon = 0; lon < 4; ++lon) {
        for (int frame = 0; frame < 10; ++frame) {
            at(s, 10.5, lon + 0.5);
            s.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        settle(s);
        REQUIRE(at(s, 10.5, lon + 0.5).has_value()); // the current tile is always there
        CHECK(s.stats().bytes <= settings.memoryBudget);
    }
    const auto stats = s.stats();
    CHECK(stats.loaded == 2);
    CHECK(stats.evictions == 2);
    CHECK(stats.loads == 4);
    CHECK_FALSE(at(s, 10.5, 0.5).has_value()); // the first one went first
    fs::remove_all(dir);
}

TEST_CASE("streamer: concurrent sampling while tiles stream in and out", "[hgt][streaming]") {
    // Mesh workers sample while the loader publishes and the main thread evicts. Values must
    // always be either "no data" or the real data - never garbage. Run under ThreadSanitizer in
    // CI, this is the data-race check of the lock-free read path.
    const fs::path dir = freshDir("orbis_stream_threads");
    for (int lon = 7; lon < 10; ++lon) {
        writeTile(dir, 45, lon, 121, plane);
    }
    TileStreamingSettings settings;
    settings.keepTicks = 1;
    settings.memoryBudget = 2 * (121 * 121 * 4 * 4 / 3) + 1000;
    HgtTileStreamer s(scanHgtDirectory(dir), settings);

    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::atomic<int> hits{0};
    std::atomic<double> window{7.05}; // the "camera": workers sample just east of it
    std::vector<std::jthread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&, t] {
            double offset = 0.07 * t;
            while (!stop.load()) {
                offset = offset + 0.011 > 0.3 ? 0.0 : offset + 0.011;
                const double lon = window.load() + offset;
                const auto v = at(s, 45.4, lon);
                if (v) {
                    ++hits;
                    if (!(std::abs(v->height - plane(45.4, lon)) < 2.0) || v->weight < 0.0 || v->weight > 1.0) {
                        ++bad;
                    }
                }
            }
        });
    }
    for (int frame = 0; frame < 600; ++frame) {
        window = 7.05 + 2.6 * frame / 600.0; // west to east, across all three tiles
        s.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stop = true;
    workers.clear();
    CHECK(bad.load() == 0);
    CHECK(hits.load() > 0);
    CHECK(s.stats().evictions > 0); // it really did stream in and out
    fs::remove_all(dir);
}

TEST_CASE("Earth with streamed tiles: base first, real data once loaded", "[hgt][streaming][earth]") {
    const fs::path dir = freshDir("orbis_stream_earth");
    writeTile(dir, 45, 7, 121, plane);
    auto streamer = std::make_shared<HgtTileStreamer>(scanHgtDirectory(dir));
    const Ellipsoid wgs84 = Ellipsoid::wgs84();
    const auto base = ElevationGrid::load(fs::path(ORBIS_TEST_DATA_DIR) / "earth_etopo10.oeg");
    const EarthTerrain earth(base, wgs84, 1, streamer);
    const EarthTerrain plain(base, wgs84, 1);
    const Vec3d where = normalize(wgs84.geodeticToEcef({45.5 * kDeg, 7.5 * kDeg, 0.0}));

    CHECK(earth.elevation(where, 30.0) == plain.elevation(where, 30.0)); // not loaded yet
    const std::uint64_t before = earth.version();
    settle(*streamer);
    CHECK(earth.version() > before); // meshes built before this are stale
    CHECK_THAT(earth.elevation(where, 30.0), WithinAbs(plane(45.5, 7.5), 15.0));
    // Coarse LOD levels never depend on streaming.
    CHECK(earth.elevation(where, 5000.0) == plain.elevation(where, 5000.0));
    fs::remove_all(dir);
}
