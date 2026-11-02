#include "core/geo/Ellipsoid.hpp"
#include "core/terrain/ChunkMesh.hpp"
#include "core/terrain/EarthTerrain.hpp"
#include "core/terrain/ElevationGrid.hpp"
#include "core/terrain/Quadtree.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kDeg = kPi / 180.0;
const Ellipsoid kWgs84 = Ellipsoid::wgs84();

/// A synthetic global grid: value = f(latitude, longitude) in degrees, 1-degree spacing.
template <typename F>
ElevationGrid makeGrid(F f, int width = 361, int height = 181) {
    std::vector<float> nodes;
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            const double lat = 90.0 - 180.0 * r / (height - 1);
            const double lon = -180.0 + 360.0 * c / (width - 1);
            nodes.push_back(static_cast<float>(f(lat, lon)));
        }
    }
    return ElevationGrid(width, height, std::move(nodes));
}

const ElevationGrid& earthGrid() {
    static const ElevationGrid grid =
        ElevationGrid::load(std::filesystem::path(ORBIS_TEST_DATA_DIR) / "earth_etopo10.oeg");
    return grid;
}

const EarthTerrain& earth() {
    static const EarthTerrain terrain(earthGrid(), kWgs84, 20261102);
    return terrain;
}

Vec3d directionAt(double latDeg, double lonDeg) {
    return normalize(kWgs84.geodeticToEcef({latDeg * kDeg, lonDeg * kDeg, 0.0}));
}
} // namespace

// ---------------------------------------------------------------------------- synthetic grids

TEST_CASE("constant grid samples constant everywhere, at every level", "[grid]") {
    const ElevationGrid g = makeGrid([](double, double) { return 123.0; });
    REQUIRE(g.levelCount() > 1);
    for (double level : {0.0, 0.5, 1.0, 2.7}) {
        for (double lat : {-90.0, -45.3, 0.0, 89.99, 90.0}) {
            for (double lon : {-180.0, -0.5, 179.9, 180.0}) {
                REQUIRE_THAT(g.sample(lat * kDeg, lon * kDeg, level), WithinAbs(123.0, 1e-4));
            }
        }
    }
}

TEST_CASE("bicubic reproduces linear functions exactly", "[grid]") {
    // Catmull-Rom is exact for linear data: no artificial bumps on uniform slopes.
    const ElevationGrid g = makeGrid([](double lat, double) { return 10.0 * lat; });
    for (double lat = -75.0; lat <= 75.0; lat += 7.37) {
        REQUIRE_THAT(g.sample(lat * kDeg, 33.3 * kDeg), WithinAbs(10.0 * lat, 1e-3));
    }
}

TEST_CASE("the date line is not an edge", "[grid]") {
    const ElevationGrid g = makeGrid([](double, double lon) { return 1000.0 * std::cos(lon * kDeg); });
    CHECK_THAT(g.sample(10 * kDeg, -kPi), WithinAbs(g.sample(10 * kDeg, kPi), 1e-3));
    // Continuous across it: values just west and just east of 180 degrees nearly agree.
    const double west = g.sample(10 * kDeg, kPi - 1e-4);
    const double east = g.sample(10 * kDeg, -kPi + 1e-4);
    CHECK_THAT(west, WithinAbs(east, 0.01));
}

TEST_CASE("the pole is a single point", "[grid]") {
    const ElevationGrid g = makeGrid([](double lat, double lon) {
        return lat > 89.5 ? 500.0 : 500.0 + (90.0 - lat) * std::sin(lon * kDeg);
    });
    const double a = g.sample(kPi / 2, 0.0);
    for (double lon : {-170.0, -60.0, 45.0, 120.0}) {
        CHECK_THAT(g.sample(kPi / 2, lon * kDeg), WithinAbs(a, 1e-3));
    }
}

TEST_CASE("coarse levels are low-pass filtered", "[grid]") {
    // A one-cell-wide spike must be strongly attenuated at level 2 (prevents aliasing at
    // coarse LOD), while the full-resolution level keeps it.
    const ElevationGrid g = makeGrid([](double lat, double lon) {
        return (std::abs(lat - 10.0) < 0.1 && std::abs(lon - 20.0) < 0.1) ? 1000.0 : 0.0;
    });
    CHECK(g.sample(10 * kDeg, 20 * kDeg, 0.0) > 999.0);
    CHECK(g.sample(10 * kDeg, 20 * kDeg, 2.0) < 200.0);
}

TEST_CASE("file loading reports problems clearly", "[grid]") {
    CHECK_THROWS(ElevationGrid::load("definitely/not/here.oeg"));

    const auto path = std::filesystem::temp_directory_path() / "orbis_bad_magic.oeg";
    {
        std::ofstream out(path, std::ios::binary);
        out << "NOPE and some more bytes to look like a header.................";
    }
    CHECK_THROWS(ElevationGrid::load(path));
    std::filesystem::remove(path);
}

// --------------------------------------------------------------------------------- real Earth

TEST_CASE("Earth data matches known geography", "[earth]") {
    const ElevationGrid& g = earthGrid();
    CHECK(g.width() == 2161);
    CHECK(g.height() == 1081);
    const auto at = [&](double lat, double lon) { return g.sample(lat * kDeg, lon * kDeg); };

    CHECK(at(27.99, 86.93) > 4500.0);   // Everest (smoothed by the 18 km grid)
    CHECK(at(32.0, 88.0) > 4000.0);     // Tibetan plateau
    CHECK(at(11.35, 142.2) < -8000.0);  // Mariana Trench
    CHECK(at(48.72, 21.26) > 0.0);      // Kosice
    CHECK(at(48.72, 21.26) < 600.0);
    CHECK(at(49.18, 20.08) > 1200.0);   // High Tatras
    CHECK(at(52.3, 5.3) < 100.0);       // Netherlands
    CHECK(at(72.0, -40.0) > 2000.0);    // Greenland ice sheet
    CHECK(at(-80.0, 0.0) > 2000.0);     // Antarctic ice sheet
    CHECK(at(0.0, -150.0) < -3000.0);   // Pacific
    CHECK(at(23.0, 10.0) > 200.0);      // Sahara
    CHECK(at(23.0, 10.0) < 1500.0);
}

TEST_CASE("Earth terrain stays within the global elevation bound", "[earth][bounds]") {
    // Densely around the Himalaya (highest data + strongest detail), plus random directions.
    for (double lat = 26.0; lat <= 36.0; lat += 0.05) {
        for (double lon = 78.0; lon <= 95.0; lon += 0.05) {
            REQUIRE(earth().elevation(directionAt(lat, lon)) < kMaxTerrainElevation);
        }
    }
    std::mt19937_64 rng(4);
    std::normal_distribution<double> gauss;
    for (int i = 0; i < 20000; ++i) {
        const Vec3d dir = normalize(Vec3d{gauss(rng), gauss(rng), gauss(rng)});
        REQUIRE(earth().elevation(dir) < kMaxTerrainElevation);
    }
}

TEST_CASE("synthetic detail is subtle on lowlands and strong in mountains", "[earth]") {
    const auto deviation = [](double lat, double lon) {
        double maxDev = 0.0;
        for (int i = 0; i < 50; ++i) {
            const double la = lat + 0.02 * i;
            const double dev = std::abs(earth().elevation(directionAt(la, lon)) -
                                        earth().measuredElevation(la * kDeg, lon * kDeg));
            maxDev = std::max(maxDev, dev);
        }
        return maxDev;
    };
    CHECK(deviation(52.2, 5.3) < 60.0);    // Netherlands: flat stays flat
    CHECK(deviation(46.0, 7.8) > 150.0);   // Alps: rugged
    // A SMALL range must be detected too (a regression once smoothed the Tatras away).
    CHECK(deviation(49.0, 20.1) > 150.0);  // High Tatras
}

TEST_CASE("Earth subtree height ranges contain every descendant", "[earth][bounds]") {
    // Same guarantee as for the procedural planet, on real mountains: culling with these ranges
    // must never remove visible terrain.
    const TerrainLod lod(kWgs84, {});
    const Vec3d alps = directionAt(46.5, 9.0);
    for (int level : {6, 9, 12, 15}) {
        // Find the node containing the Alps at this level.
        for (const CubeFace face : kAllCubeFaces) {
            const FaceBasis& b = faceBasis(face);
            const double along = dot(alps, b.normal);
            if (along < 0.5) {
                continue;
            }
            const Vec3d onCube = alps / along; // project onto the face plane
            const double u = dot(onCube, b.uAxis);
            const double v = dot(onCube, b.vAxis);
            if (std::abs(u) > 1.0 || std::abs(v) > 1.0) {
                continue;
            }
            const double cells = static_cast<double>(1u << level);
            const NodeKey parent{face, static_cast<std::uint8_t>(level),
                                 static_cast<std::uint32_t>((u + 1.0) / 2.0 * cells),
                                 static_cast<std::uint32_t>((v + 1.0) / 2.0 * cells)};
            const ChunkMesh pm = buildChunkMesh(parent.region(), 32, kWgs84, earth());
            const HeightRange range = lod.subtreeRange(parent, {pm.minHeight, pm.maxHeight});
            for (std::uint32_t c = 0; c < 4; ++c) {
                const ChunkMesh m =
                    buildChunkMesh(parent.child(c & 1u, c >> 1u).region(), 32, kWgs84, earth());
                REQUIRE(m.minHeight >= range.min);
                REQUIRE(m.maxHeight <= range.max);
            }
        }
    }
}

TEST_CASE("synthetic detail never turns measured land into sea", "[earth]") {
    // Low plains just above sea level (Po valley, Hungary, Netherlands polders above 0).
    std::mt19937_64 rng(9);
    std::uniform_real_distribution<double> jitter(-0.5, 0.5);
    for (const auto& [lat, lon] : {std::pair{45.1, 11.5}, {47.0, 20.0}, {52.6, 5.9}}) {
        for (int i = 0; i < 2000; ++i) {
            const double la = lat + jitter(rng);
            const double lo = lon + jitter(rng);
            if (earth().measuredElevation(la * kDeg, lo * kDeg) > 0.0) {
                REQUIRE(earth().elevation(directionAt(la, lo)) > 0.0);
            }
        }
    }
}
