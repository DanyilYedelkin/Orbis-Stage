#include "core/terrain/HeightField.hpp"
#include "core/terrain/Noise.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <random>

using namespace orbis;
using Catch::Matchers::WithinAbs;

TEST_CASE("noise is deterministic for a seed", "[noise]") {
    const PerlinNoise a(123);
    const PerlinNoise b(123);
    const PerlinNoise c(124);
    const Vec3d p{1.37, -4.2, 9.81};
    CHECK(a.sample(p) == b.sample(p));
    CHECK(a.sample(p) != c.sample(p));
}

TEST_CASE("noise is zero on integer lattice points", "[noise]") {
    const PerlinNoise n(7);
    CHECK_THAT(n.sample({3.0, -2.0, 5.0}), WithinAbs(0.0, 1e-12));
}

TEST_CASE("noise and fBm stay in range", "[noise]") {
    const PerlinNoise n(99);
    std::mt19937_64 rng(1);
    std::uniform_real_distribution<double> coord(-1000.0, 1000.0);
    for (int i = 0; i < 100000; ++i) {
        const Vec3d p{coord(rng), coord(rng), coord(rng)};
        REQUIRE(std::abs(n.sample(p)) <= 1.1);
        REQUIRE(std::abs(fbm(n, p, {})) <= 1.1);
    }
}

TEST_CASE("height field produces both oceans and mountains", "[noise][terrain]") {
    const HeightField field(20261102);
    std::mt19937_64 rng(5);
    std::normal_distribution<double> gauss;
    int land = 0;
    double highest = -1e9;
    double lowest = 1e9;
    constexpr int kSamples = 20000;
    for (int i = 0; i < kSamples; ++i) {
        const Vec3d dir = normalize(Vec3d{gauss(rng), gauss(rng), gauss(rng)}); // uniform on sphere
        const double h = field.elevation(dir);
        land += h > 0.0 ? 1 : 0;
        highest = std::max(highest, h);
        lowest = std::min(lowest, h);
    }
    const double landFraction = static_cast<double>(land) / kSamples;
    CHECK(landFraction > 0.2);
    CHECK(landFraction < 0.6);
    CHECK(highest > 3000.0);
    CHECK(highest < 12000.0);
    CHECK(lowest < -2000.0);
}

TEST_CASE("ridged multifractal stays in [0, 1] and respects the band limit", "[noise]") {
    const PerlinNoise n(5);
    std::mt19937_64 rng(2);
    std::uniform_real_distribution<double> coord(-100.0, 100.0);
    for (int i = 0; i < 20000; ++i) {
        const Vec3d p{coord(rng), coord(rng), coord(rng)};
        const double full = ridgedFbm(n, p, {.octaves = 8});
        REQUIRE(full >= 0.0);
        REQUIRE(full <= 1.0);
        // With every octave filtered out (limit below the first octave), nothing remains.
        REQUIRE(ridgedFbm(n, p, {.octaves = 8}, 0.5) == 0.0);
    }
}

TEST_CASE("band limiting keeps the low frequencies identical", "[noise]") {
    // Coarse and fine LOD levels must agree on the overall shape, or terrain would "breathe"
    // when switching levels: removing high octaves may not rescale the remaining ones.
    // Limit 2.0: octave 1 (frequency 1) gets full weight, octave 2 (frequency 2) is faded out.
    const PerlinNoise n(8);
    const Vec3d p{1.3, 2.7, -0.4};
    const FbmParams one{.octaves = 1};
    const FbmParams six{.octaves = 6};
    const double firstOctaveOnly = fbm(n, p, one) * 1.0 / (1.0 + 0.5 + 0.25 + 0.125 + 0.0625 + 0.03125);
    CHECK_THAT(fbm(n, p, six, 2.0), WithinAbs(firstOctaveOnly, 1e-12));
}
