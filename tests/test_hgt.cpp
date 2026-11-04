#include "core/geo/Ellipsoid.hpp"
#include "core/terrain/EarthTerrain.hpp"
#include "core/terrain/HgtTiles.hpp"
#include "hgt_test_tiles.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numbers>
#include <string>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
namespace fs = std::filesystem;
using orbis::test::freshDir;
using orbis::test::writeTile;

// Elevation that varies differently along latitude and longitude: a swapped axis, a mirrored
// tile or a wrong byte order would all produce clearly wrong numbers.
double plane(double lat, double lon) { return 1000.0 * (lat - 45.0) + 200.0 * (lon - 7.0) + 100.0; }
} // namespace

TEST_CASE("hgt tile names", "[hgt]") {
    CHECK(parseHgtName("N45E007") == std::pair{45, 7});
    CHECK(parseHgtName("s12w077") == std::pair{-12, -77});
    CHECK_FALSE(parseHgtName("N45E07").has_value());
    CHECK_FALSE(parseHgtName("X45E007").has_value());
    CHECK_FALSE(parseHgtName("readme_").has_value());
}

TEST_CASE("hgt mosaic: orientation, byte order and stitching", "[hgt]") {
    const fs::path dir = freshDir("orbis_hgt_plane");
    writeTile(dir, 45, 7, 61, plane);
    writeTile(dir, 45, 8, 61, plane);
    const auto mosaic = loadHgtMosaic(dir);
    REQUIRE(mosaic.has_value());
    CHECK(mosaic->tileCount == 2);
    CHECK(mosaic->samplesPerTile == 61);
    CHECK(mosaic->grid.width() == 121); // two tiles share their edge column
    CHECK(mosaic->grid.height() == 61);

    for (const auto& [lat, lon] : {std::pair{45.1, 7.2}, {45.9, 7.05}, {45.5, 8.7}, {45.02, 8.98}}) {
        REQUIRE_THAT(mosaic->grid.sample(lat * kDeg, lon * kDeg), WithinAbs(plane(lat, lon), 1.0));
    }
    // Seamless across the tile border at 8 degrees east.
    const double west = mosaic->grid.sample(45.3 * kDeg, (8.0 - 1e-6) * kDeg);
    const double east = mosaic->grid.sample(45.3 * kDeg, (8.0 + 1e-6) * kDeg);
    CHECK_THAT(west, WithinAbs(east, 0.1));
    fs::remove_all(dir);
}

TEST_CASE("hgt voids and missing tiles are NaN, not invented", "[hgt]") {
    const fs::path dir = freshDir("orbis_hgt_void");
    writeTile(dir, 45, 7, 61, [](double lat, double lon) {
        return (lat > 45.4 && lat < 45.6 && lon > 7.4 && lon < 7.6) ? std::nan("") : 500.0;
    });
    writeTile(dir, 46, 8, 61, [](double, double) { return 500.0; }); // diagonal: 2 tiles missing
    const auto mosaic = loadHgtMosaic(dir);
    REQUIRE(mosaic.has_value());
    CHECK(std::isnan(mosaic->grid.sample(45.5 * kDeg, 7.5 * kDeg)));  // void
    CHECK(std::isnan(mosaic->grid.sample(46.5 * kDeg, 7.5 * kDeg)));  // tile not present
    CHECK_THAT(mosaic->grid.sample(45.2 * kDeg, 7.2 * kDeg), WithinAbs(500.0, 0.5));
    fs::remove_all(dir);
}

TEST_CASE("no tiles: no mosaic; bad tiles: a clear error", "[hgt]") {
    const fs::path empty = freshDir("orbis_hgt_empty");
    CHECK_FALSE(loadHgtMosaic(empty).has_value());
    std::ofstream(empty / "N45E007.hgt", std::ios::binary) << "not a square tile";
    CHECK_THROWS(loadHgtMosaic(empty));
    fs::remove_all(empty);
}

TEST_CASE("Earth uses the real tiles inside and blends smoothly at their edge", "[hgt][earth]") {
    const fs::path dir = freshDir("orbis_hgt_earth");
    writeTile(dir, 45, 7, 121, plane);
    auto mosaic = loadHgtMosaic(dir);
    REQUIRE(mosaic.has_value());
    const Ellipsoid wgs84 = Ellipsoid::wgs84();
    const auto base = ElevationGrid::load(fs::path(ORBIS_TEST_DATA_DIR) / "earth_etopo10.oeg");
    const EarthTerrain withTiles(base, wgs84, 1, std::move(mosaic->grid));
    const EarthTerrain withoutTiles(base, wgs84, 1);
    const auto at = [&](const EarthTerrain& t, double lat, double lon) {
        return t.elevation(normalize(wgs84.geodeticToEcef({lat * kDeg, lon * kDeg, 0.0})));
    };

    // Deep inside: the real data (plus a few metres of sub-resolution detail).
    CHECK_THAT(at(withTiles, 45.5, 7.5), WithinAbs(plane(45.5, 7.5), 15.0));
    // Far outside: exactly the base terrain.
    CHECK(at(withTiles, 47.5, 9.5) == at(withoutTiles, 47.5, 9.5));
    // Across the edge (7 deg E), walking in 10 m steps: no cliff, only the gradual blend.
    double worstStep = 0.0;
    double previous = at(withTiles, 45.5, 6.95);
    for (int i = 1; i <= 800; ++i) {
        const double lon = 6.95 + i * 10.0 / (111320.0 * std::cos(45.5 * kDeg));
        const double h = at(withTiles, 45.5, lon);
        worstStep = std::max(worstStep, std::abs(h - previous));
        previous = h;
    }
    CHECK(worstStep < 40.0);
    fs::remove_all(dir);
}

TEST_CASE("no cliff at the edge of a missing tile inside the mosaic", "[hgt][earth]") {
    // Tiles on a diagonal leave two holes inside the bounding box. Walking from real data into a
    // hole must blend, not jump.
    const fs::path dir = freshDir("orbis_hgt_hole");
    writeTile(dir, 45, 7, 121, [](double, double) { return 3000.0; });
    writeTile(dir, 46, 8, 121, [](double, double) { return 3000.0; });
    auto mosaic = loadHgtMosaic(dir);
    REQUIRE(mosaic.has_value());
    const Ellipsoid wgs84 = Ellipsoid::wgs84();
    const auto base = ElevationGrid::load(fs::path(ORBIS_TEST_DATA_DIR) / "earth_etopo10.oeg");
    const EarthTerrain terrain(base, wgs84, 1, std::move(mosaic->grid));
    const auto at = [&](double lat, double lon) {
        return terrain.elevation(normalize(wgs84.geodeticToEcef({lat * kDeg, lon * kDeg, 0.0})));
    };
    // From the N45E007 tile north into the missing N46E007 square, across 46 N.
    double worstStep = 0.0;
    double previous = at(45.9, 7.5);
    for (int i = 1; i <= 2200; ++i) {
        const double h = at(45.9 + i * 10.0 / 111320.0, 7.5);
        worstStep = std::max(worstStep, std::abs(h - previous));
        previous = h;
    }
    CHECK(worstStep < 40.0);
    CHECK_THAT(at(45.3, 7.3), WithinAbs(3000.0, 15.0)); // well inside: real data
    fs::remove_all(dir);
}
