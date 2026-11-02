#include "core/scene/Camera.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numbers>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
}

TEST_CASE("camera basis is orthonormal", "[camera]") {
    Camera cam(Ellipsoid::wgs84());
    cam.setPosition({48.7 * kDeg, 21.3 * kDeg, 1000.0});
    cam.setOrientation(37.0 * kDeg, -12.0 * kDeg);
    const CameraBasis b = cam.basis();

    CHECK_THAT(length(b.forward), WithinAbs(1.0, 1e-12));
    CHECK_THAT(length(b.right), WithinAbs(1.0, 1e-12));
    CHECK_THAT(length(b.up), WithinAbs(1.0, 1e-12));
    CHECK_THAT(dot(b.forward, b.right), WithinAbs(0.0, 1e-12));
    CHECK_THAT(dot(b.forward, b.up), WithinAbs(0.0, 1e-12));
    CHECK_THAT(dot(b.right, b.enu.up), WithinAbs(0.0, 1e-12)); // no roll: right is horizontal
}

TEST_CASE("heading 0 looks north, heading 90 looks east", "[camera]") {
    Camera cam(Ellipsoid::wgs84());
    cam.setPosition({10.0 * kDeg, 50.0 * kDeg, 500.0});

    cam.setOrientation(0.0, 0.0);
    CHECK_THAT(dot(cam.basis().forward, cam.basis().enu.north), WithinAbs(1.0, 1e-12));
    cam.setOrientation(90.0 * kDeg, 0.0);
    CHECK_THAT(dot(cam.basis().forward, cam.basis().enu.east), WithinAbs(1.0, 1e-12));
}

TEST_CASE("pitch is clamped short of vertical", "[camera]") {
    Camera cam(Ellipsoid::wgs84());
    cam.setOrientation(0.0, 3.0);
    CHECK(cam.pitch() < std::numbers::pi / 2);
}

TEST_CASE("altitude never goes below the minimum", "[camera]") {
    Camera cam(Ellipsoid::wgs84());
    cam.setPosition({0.0, 0.0, 100.0});
    cam.translate({-1.0e6, 0.0, 0.0}); // dive straight into the planet
    CHECK(cam.basis().geodetic.height >= cam.minAltitude - 1e-6);
}

TEST_CASE("flying over the pole keeps the view direction continuous", "[camera]") {
    Camera cam(Ellipsoid::wgs84());
    cam.setPosition({89.9 * kDeg, 0.0, 1000.0});
    cam.setOrientation(0.0, 0.0); // heading north, towards the pole

    for (int i = 0; i < 100; ++i) {
        const Vec3d before = cam.basis().forward;
        cam.translate(before * 500.0); // 50 km total, straight across the pole
        const Vec3d after = cam.basis().forward;
        REQUIRE(dot(before, after) > 0.99); // never a sudden flip
    }
    // On the other side "north" is behind us, so the heading must now be ~180 degrees.
    CHECK(std::abs(cam.heading()) > 170.0 * kDeg);
}
