#include "core/geo/Ellipsoid.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <random>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
const Ellipsoid kWgs84 = Ellipsoid::wgs84();
} // namespace

TEST_CASE("WGS84 constants", "[geo]") {
    CHECK(kWgs84.semiMajorAxis() == 6378137.0);
    CHECK_THAT(kWgs84.semiMinorAxis(), WithinAbs(6356752.314245, 1e-6));
    CHECK_THAT(kWgs84.eccentricitySquared(), WithinAbs(0.00669437999014, 1e-14));
}

TEST_CASE("geodetic -> ECEF at reference points", "[geo]") {
    const Vec3d equator = kWgs84.geodeticToEcef({0.0, 0.0, 0.0});
    CHECK_THAT(equator.x, WithinAbs(6378137.0, 1e-6));
    CHECK_THAT(equator.y, WithinAbs(0.0, 1e-6));
    CHECK_THAT(equator.z, WithinAbs(0.0, 1e-6));

    const Vec3d pole = kWgs84.geodeticToEcef({90.0 * kDeg, 0.0, 100.0});
    CHECK_THAT(pole.x, WithinAbs(0.0, 1e-6));
    CHECK_THAT(pole.z, WithinAbs(kWgs84.semiMinorAxis() + 100.0, 1e-6));

    const Vec3d east = kWgs84.geodeticToEcef({0.0, 90.0 * kDeg, 0.0});
    CHECK_THAT(east.y, WithinAbs(6378137.0, 1e-6));
}

TEST_CASE("ECEF -> geodetic round trip is sub-millimetre everywhere", "[geo]") {
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> lat(-90.0, 90.0);
    std::uniform_real_distribution<double> lon(-180.0, 180.0);
    std::uniform_real_distribution<double> height(-11000.0, 1.0e7); // Mariana trench .. GEO-ish

    for (int i = 0; i < 20000; ++i) {
        const Geodetic in{lat(rng) * kDeg, lon(rng) * kDeg, height(rng)};
        const Vec3d ecef = kWgs84.geodeticToEcef(in);
        const Geodetic out = kWgs84.ecefToGeodetic(ecef);
        // Compare in ECEF: an angle error at the pole is meaningless for longitude.
        REQUIRE(distance(kWgs84.geodeticToEcef(out), ecef) < 1e-3);
        REQUIRE(std::abs(out.height - in.height) < 1e-3);
    }
}

TEST_CASE("ECEF -> geodetic is stable exactly at the poles", "[geo]") {
    const double h = GENERATE(-500.0, 0.0, 8848.0, 400000.0);
    const Geodetic north = kWgs84.ecefToGeodetic({0.0, 0.0, kWgs84.semiMinorAxis() + h});
    CHECK_THAT(north.latitude, WithinAbs(std::numbers::pi / 2, 1e-12));
    CHECK_THAT(north.height, WithinAbs(h, 1e-6));

    const Geodetic south = kWgs84.ecefToGeodetic({0.0, 0.0, -kWgs84.semiMinorAxis() - h});
    CHECK_THAT(south.latitude, WithinAbs(-std::numbers::pi / 2, 1e-12));
    CHECK_THAT(south.height, WithinAbs(h, 1e-6));
}

TEST_CASE("ENU frame is orthonormal and right-handed", "[geo]") {
    const double lat = GENERATE(-89.9, -45.0, 0.0, 48.7164, 90.0);
    const double lon = GENERATE(-170.0, 0.0, 21.2611);
    const EnuFrame f = Ellipsoid::enuFrame(lat * kDeg, lon * kDeg);

    CHECK_THAT(length(f.east), WithinAbs(1.0, 1e-12));
    CHECK_THAT(length(f.north), WithinAbs(1.0, 1e-12));
    CHECK_THAT(dot(f.east, f.north), WithinAbs(0.0, 1e-12));
    const Vec3d up = cross(f.east, f.north);
    CHECK_THAT(distance(up, f.up), WithinAbs(0.0, 1e-12));
}

TEST_CASE("ENU up equals the ellipsoid surface normal", "[geo]") {
    const Geodetic g{48.7164 * kDeg, 21.2611 * kDeg, 0.0};
    const Vec3d normal = kWgs84.surfaceNormal(kWgs84.geodeticToEcef(g));
    const EnuFrame f = Ellipsoid::enuFrame(g.latitude, g.longitude);
    CHECK_THAT(distance(normal, f.up), WithinAbs(0.0, 1e-12));
}

TEST_CASE("surfacePointAlong lands on the ellipsoid", "[geo]") {
    const Vec3d dir = normalize(Vec3d{0.3, -0.5, 0.8});
    const Vec3d p = kWgs84.surfacePointAlong(dir);
    const double a = kWgs84.semiMajorAxis();
    const double b = kWgs84.semiMinorAxis();
    const double implicit = (p.x * p.x + p.y * p.y) / (a * a) + (p.z * p.z) / (b * b);
    CHECK_THAT(implicit, WithinAbs(1.0, 1e-14));
    CHECK_THAT(kWgs84.ecefToGeodetic(p).height, WithinAbs(0.0, 1e-6));
}
