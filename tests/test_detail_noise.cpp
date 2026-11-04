#include "core/terrain/DetailNoise.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <random>

using namespace orbis;
using Catch::Matchers::WithinAbs;

TEST_CASE("periodic gradient noise: analytic derivative matches finite differences", "[detail]") {
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> uniform(-2.0, 2.0);
    for (int period : {1, 3, 8, 64}) {
        for (int i = 0; i < 2000; ++i) {
            const double u = uniform(rng);
            const double v = uniform(rng);
            const PeriodicNoiseSample s = periodicGradientNoise(u, v, period, 42);
            const double h = 1e-6 / period;
            const double fu = (periodicGradientNoise(u + h, v, period, 42).value -
                               periodicGradientNoise(u - h, v, period, 42).value) / (2.0 * h);
            const double fv = (periodicGradientNoise(u, v + h, period, 42).value -
                               periodicGradientNoise(u, v - h, period, 42).value) / (2.0 * h);
            REQUIRE_THAT(s.du, WithinAbs(fu, 1e-5 * period));
            REQUIRE_THAT(s.dv, WithinAbs(fv, 1e-5 * period));
        }
    }
}

TEST_CASE("periodic gradient noise repeats with period 1 in tile units", "[detail]") {
    std::mt19937_64 rng(11);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    for (int i = 0; i < 500; ++i) {
        const double u = uniform(rng);
        const double v = uniform(rng);
        const double a = periodicGradientNoise(u, v, 16, 5).value;
        // Exactly periodic up to the rounding of u + k in double.
        REQUIRE_THAT(periodicGradientNoise(u + 1.0, v, 16, 5).value, WithinAbs(a, 1e-9));
        REQUIRE_THAT(periodicGradientNoise(u, v - 3.0, 16, 5).value, WithinAbs(a, 1e-9));
    }
}

TEST_CASE("detail noise texture is normalised, tiles seamlessly and carries its derivatives",
          "[detail]") {
    const int n = 128;
    const DetailNoise t = makeDetailNoise(n, 20261104);
    REQUIRE(t.texels.size() == static_cast<std::size_t>(n * n * 4));

    for (int channel : {0, 3}) {
        double sum = 0.0;
        double sumSq = 0.0;
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                sum += t.at(x, y, channel);
                sumSq += t.at(x, y, channel) * t.at(x, y, channel);
            }
        }
        const double mean = sum / (n * n);
        CHECK_THAT(mean, WithinAbs(0.0, 1e-5));
        CHECK_THAT(std::sqrt(sumSq / (n * n) - mean * mean), WithinAbs(1.0, 1e-4));
    }

    // Seam: the step from the last column to the first (and last row to first) must look like any
    // other step between neighbours - a non-tiling texture shows a cliff here.
    double interiorMax = 0.0;
    double seamMax = 0.0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double right = std::abs(t.at((x + 1) % n, y, 0) - t.at(x, y, 0));
            const double down = std::abs(t.at(x, (y + 1) % n, 0) - t.at(x, y, 0));
            const bool seam = x == n - 1 || y == n - 1;
            (seam ? seamMax : interiorMax) = std::max(seam ? seamMax : interiorMax, std::max(right, down));
        }
    }
    CHECK(seamMax <= interiorMax);

    // The stored derivatives agree with central differences of the stored height, ACROSS the
    // seam too (tile units: one texel is 1/n). At the renderer's 512^2 the finest octave has 4
    // texels per lattice cell, so central differences are a fair (if approximate) reference.
    const int m = 512;
    const DetailNoise big = makeDetailNoise(m, 20261104);
    double worst = 0.0;
    double scale = 0.0;
    for (int y = 0; y < m; ++y) {
        for (int x = 0; x < m; ++x) {
            const double du = (big.at((x + 1) % m, y, 0) - big.at((x + m - 1) % m, y, 0)) * m / 2.0;
            const double dv = (big.at(x, (y + 1) % m, 0) - big.at(x, (y + m - 1) % m, 0)) * m / 2.0;
            worst = std::max({worst, std::abs(du - big.at(x, y, 1)), std::abs(dv - big.at(x, y, 2))});
            scale = std::max(scale, std::abs(big.at(x, y, 1)));
        }
    }
    CHECK(worst < 0.08 * scale);

    // gradientStd is what it says, and the noise is isotropic (du and dv alike).
    double sumU = 0.0;
    double sumV = 0.0;
    for (int i = 0; i < m * m; ++i) {
        sumU += big.at(i % m, i / m, 1) * big.at(i % m, i / m, 1);
        sumV += big.at(i % m, i / m, 2) * big.at(i % m, i / m, 2);
    }
    CHECK_THAT(std::sqrt(sumU / (m * m)), WithinAbs(big.gradientStd, 0.05 * big.gradientStd));
    CHECK_THAT(std::sqrt(sumV / (m * m)), WithinAbs(big.gradientStd, 0.05 * big.gradientStd));

    // Deterministic.
    CHECK(makeDetailNoise(64, 3).texels == makeDetailNoise(64, 3).texels);
    CHECK(makeDetailNoise(64, 3).texels != makeDetailNoise(64, 4).texels);
    CHECK_THROWS(makeDetailNoise(100, 1));
}
