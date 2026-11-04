#include "core/atmosphere/Clouds.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <random>
#include <string>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
bool same(const Vec3d& a, const Vec3d& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
constexpr double kGround = 6360.0;
constexpr double kInner = kGround + 1.5;
constexpr double kOuter = kGround + 4.5; // fixed test geometry, not the defaults
} // namespace

TEST_CASE("ray-shell intervals: from the ground, inside, and from space", "[clouds]") {
    SECTION("straight up from the ground: one interval, base to top") {
        const ShellIntervals s = rayShellIntervals({0, 0, kGround + 0.001}, {0, 0, 1}, kInner, kOuter, kGround);
        REQUIRE(s.count == 1);
        CHECK_THAT(s.t0[0], WithinAbs(1.499, 1e-9));
        CHECK_THAT(s.t1[0], WithinAbs(4.499, 1e-9));
    }
    SECTION("horizontal from the ground: through the shell once, upwards") {
        const double r = kGround;
        const ShellIntervals s = rayShellIntervals({0, 0, r}, {1, 0, 0}, kInner, kOuter, kGround);
        REQUIRE(s.count == 1);
        CHECK_THAT(s.t0[0], WithinAbs(std::sqrt(kInner * kInner - r * r), 1e-6));
        CHECK_THAT(s.t1[0], WithinAbs(std::sqrt(kOuter * kOuter - r * r), 1e-6));
    }
    SECTION("inside the shell looking up: starts at the eye") {
        const ShellIntervals s = rayShellIntervals({0, 0, kGround + 3.0}, {0, 0, 1}, kInner, kOuter, kGround);
        REQUIRE(s.count == 1);
        CHECK(s.t0[0] == 0.0);
        CHECK_THAT(s.t1[0], WithinAbs(1.5, 1e-9));
    }
    SECTION("from space straight down: stops at the ground, the far side does not count") {
        const ShellIntervals s = rayShellIntervals({0, 0, kGround + 400.0}, {0, 0, -1}, kInner, kOuter, kGround);
        REQUIRE(s.count == 1);
        CHECK_THAT(s.t0[0], WithinAbs(395.5, 1e-9));
        CHECK_THAT(s.t1[0], WithinAbs(398.5, 1e-9));
    }
    SECTION("from space, grazing the layer above the cloud base: one chord") {
        const double b = kInner + 1.0; // closest approach to the centre
        const ShellIntervals s = rayShellIntervals({-3000.0, b, 0.0}, {1, 0, 0}, kInner, kOuter, kGround);
        REQUIRE(s.count == 1);
        const double half = std::sqrt(kOuter * kOuter - b * b);
        CHECK_THAT(s.t0[0], WithinAbs(3000.0 - half, 1e-6));
        CHECK_THAT(s.t1[0], WithinAbs(3000.0 + half, 1e-6));
    }
    SECTION("from space, dipping below the cloud base without reaching the ground: two") {
        const double b = kGround + 0.7; // between ground and cloud base
        const ShellIntervals s = rayShellIntervals({-3000.0, b, 0.0}, {1, 0, 0}, kInner, kOuter, kGround);
        REQUIRE(s.count == 2);
        const double outer = std::sqrt(kOuter * kOuter - b * b);
        const double inner = std::sqrt(kInner * kInner - b * b);
        CHECK_THAT(s.t0[0], WithinAbs(3000.0 - outer, 1e-6));
        CHECK_THAT(s.t1[0], WithinAbs(3000.0 - inner, 1e-6));
        CHECK_THAT(s.t0[1], WithinAbs(3000.0 + inner, 1e-6));
        CHECK_THAT(s.t1[1], WithinAbs(3000.0 + outer, 1e-6));
    }
    SECTION("above the layer looking away: nothing") {
        const ShellIntervals s = rayShellIntervals({0, 0, kGround + 10.0}, {0, 0, 1}, kInner, kOuter, kGround);
        CHECK(s.count == 0);
    }
}

TEST_CASE("cube map directions follow the OpenGL face table and invert exactly", "[clouds]") {
    // Face centres point along the axes in GL order.
    const Vec3d axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        const Vec3d c = cubeMapDirection(f, 0.5, 0.5);
        CHECK(same(c, axes[f]));
    }
    // Spot checks against the spec table: +X face, s grows towards -z, t towards -y.
    CHECK(same(cubeMapDirection(0, 1.0, 0.5), {1, 0, -1}));
    CHECK(same(cubeMapDirection(0, 0.5, 1.0), {1, -1, 0}));
    CHECK(same(cubeMapDirection(2, 0.5, 1.0), {0, 1, 1})); // +Y: t towards +z

    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(0.001, 0.999);
    for (int i = 0; i < 3000; ++i) {
        const int face = i % 6;
        const double s = u(rng);
        const double t = u(rng);
        int f2 = -1;
        double s2 = 0.0;
        double t2 = 0.0;
        cubeMapLookup(cubeMapDirection(face, s, t) * 3.7, f2, s2, t2);
        REQUIRE(f2 == face);
        REQUIRE_THAT(s2, WithinAbs(s, 1e-12));
        REQUIRE_THAT(t2, WithinAbs(t, 1e-12));
    }
}

TEST_CASE("weather follows the climatology: clear subtropics, cloudy storm tracks", "[clouds]") {
    const int n = 64;
    const std::vector<float> w = makeCloudWeather(n, 1234);
    REQUIRE(w.size() == static_cast<std::size_t>(n * n * 6));
    double sub = 0.0;
    double storm = 0.0;
    int subCount = 0;
    int stormCount = 0;
    std::size_t i = 0;
    for (int f = 0; f < 6; ++f) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x, ++i) {
                REQUIRE(w[i] >= 0.0f);
                REQUIRE(w[i] <= 1.0f);
                const Vec3d d = normalize(cubeMapDirection(f, (x + 0.5) / n, (y + 0.5) / n));
                const double lat = std::abs(std::asin(d.z)) * 180.0 / std::numbers::pi;
                if (lat > 18.0 && lat < 32.0) {
                    sub += static_cast<double>(w[i]);
                    ++subCount;
                } else if (lat > 48.0 && lat < 65.0) {
                    storm += static_cast<double>(w[i]);
                    ++stormCount;
                }
            }
        }
    }
    CHECK(storm / stormCount > sub / subCount + 0.15);
    CHECK(cloudClimatology(25.0 * std::numbers::pi / 180.0) < 0.0);
    CHECK(cloudClimatology(57.0 * std::numbers::pi / 180.0) > 0.1);
    CHECK(makeCloudWeather(16, 7) == makeCloudWeather(16, 7));
}

TEST_CASE("cloud GLSL has every parameter and rejects inconsistent tiles", "[clouds]") {
    const std::string glsl = toGlsl(CloudParameters{});
    for (const char* name : {"kCloudBottom", "kCloudTop", "kCloudExtinction", "kCloudShapeTile",
                             "kCloudDetailTile", "kCloudPhaseForward"}) {
        CHECK(glsl.find(name) != std::string::npos);
    }
    CloudParameters bad;
    bad.detailTile = 5.0; // does not divide 12 km: the detail would jump at the period wrap
    CHECK_THROWS(toGlsl(bad));
    CloudParameters bad2;
    bad2.regionalTile = 50.0; // not a multiple of the 12 km shapes
    CHECK_THROWS(toGlsl(bad2));
}
