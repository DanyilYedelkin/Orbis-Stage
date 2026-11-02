#include "core/geo/Ellipsoid.hpp"
#include "core/geo/Horizon.hpp"
#include "core/math/Frustum.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace orbis;

TEST_CASE("frustum keeps what is in front and rejects what is behind or aside", "[culling]") {
    const Vec3d forward{0, 0, -1};
    const Vec3d right{1, 0, 0};
    const Vec3d up{0, 1, 0};
    const Frustum f = Frustum::fromCamera(forward, right, up, std::numbers::pi / 3.0, 1.0, 0.5);

    CHECK(f.intersectsSphere({0, 0, -100}, 1.0));   // straight ahead
    CHECK_FALSE(f.intersectsSphere({0, 0, 100}, 1.0)); // behind
    CHECK_FALSE(f.intersectsSphere({200, 0, -100}, 1.0)); // far to the right (half-angle 30 deg)
    CHECK(f.intersectsSphere({200, 0, -100}, 150.0));     // ... unless it is big enough to reach in
    CHECK_FALSE(f.intersectsSphere({0, 0, -0.1}, 0.1));   // in front of the near plane
}

TEST_CASE("frustum side planes contain the frustum edges", "[culling]") {
    const Vec3d forward{0, 0, -1};
    const Vec3d right{1, 0, 0};
    const Vec3d up{0, 1, 0};
    const double fov = 1.0;
    const double aspect = 1.5;
    const Frustum f = Frustum::fromCamera(forward, right, up, fov, aspect, 0.5);
    const double tanY = std::tan(fov / 2.0);
    const double tanX = tanY * aspect;
    // A point exactly on the left edge direction lies on the left plane.
    const Vec3d leftEdge = (forward - right * tanX) * 10.0;
    CHECK(std::abs(f.planes()[0].signedDistance(leftEdge)) < 1e-12);
    const Vec3d topEdge = (forward + up * tanY) * 10.0;
    CHECK(std::abs(f.planes()[3].signedDistance(topEdge)) < 1e-12);
}

TEST_CASE("horizon culling", "[culling]") {
    const double r = 6356752.0;
    const Vec3d camera{r + 1000.0e3, 0, 0}; // 1000 km above the equator

    CHECK_FALSE(isBelowHorizon(camera, {r, 0, 0}, 10.0, r));       // directly below
    CHECK(isBelowHorizon(camera, {-r, 0, 0}, 10.0, r));            // antipode
    CHECK(isBelowHorizon(camera, {0, 0, 0}, 100.0, r));            // inside the planet

    // Geometric horizon from 1000 km: angle acos(r / (r + h)) ~ 29.6 degrees from the nadir.
    const auto onSurfaceAt = [r](double degrees) {
        const double a = degrees * std::numbers::pi / 180.0;
        return Vec3d{r * std::cos(a), r * std::sin(a), 0.0};
    };
    CHECK_FALSE(isBelowHorizon(camera, onSurfaceAt(29.0), 1.0, r)); // just inside the horizon
    CHECK(isBelowHorizon(camera, onSurfaceAt(31.0), 1.0, r));       // just beyond

    // A tall object beyond the geometric horizon still sticks up into view.
    const Vec3d beyond = onSurfaceAt(33.0);
    CHECK(isBelowHorizon(camera, beyond, 1.0, r));
    CHECK_FALSE(isBelowHorizon(camera, beyond * ((r + 150.0e3) / r), 1.0, r));
}

TEST_CASE("ellipsoid horizon is exact, not a smaller inscribed sphere", "[culling]") {
    const Ellipsoid e = Ellipsoid::wgs84();
    const double deg = std::numbers::pi / 180.0;
    // 300 m above the equator the horizon is ~62 km away (sqrt(2 * R * h)).
    const Vec3d camera = e.geodeticToEcef({0.0, 0.0, 300.0});
    const auto surfaceAt = [&](double km) {
        return e.geodeticToEcef({0.0, km * 1000.0 / e.semiMajorAxis() / deg * deg, 0.0});
    };
    CHECK_FALSE(isBelowHorizon(camera, surfaceAt(50.0), 1.0, e));
    CHECK(isBelowHorizon(camera, surfaceAt(80.0), 1.0, e));
    // The inscribed polar-radius sphere is 21 km too small at the equator: from 300 m it would
    // let us "see" ~520 km, keeping hundreds of invisible chunks alive.
    CHECK_FALSE(isBelowHorizon(camera, surfaceAt(80.0), 1.0, e.semiMinorAxis()));
}
