#include "core/math/Vec3.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace orbis;
using Catch::Matchers::WithinAbs;

TEST_CASE("Vec3 arithmetic", "[math]") {
    const Vec3d a{1, 2, 3};
    const Vec3d b{4, 5, 6};
    CHECK(dot(a, b) == 32.0);
    const Vec3d s = a + b * 2.0;
    CHECK(s.x == 9.0);
    CHECK(s.y == 12.0);
    CHECK(s.z == 15.0);
}

TEST_CASE("cross product is right-handed and orthogonal", "[math]") {
    const Vec3d z = cross(Vec3d{1, 0, 0}, Vec3d{0, 1, 0});
    CHECK(z.x == 0.0);
    CHECK(z.y == 0.0);
    CHECK(z.z == 1.0);

    const Vec3d a{0.3, -1.7, 2.2};
    const Vec3d b{-4.1, 0.5, 0.9};
    const Vec3d c = cross(a, b);
    CHECK_THAT(dot(c, a), WithinAbs(0.0, 1e-12));
    CHECK_THAT(dot(c, b), WithinAbs(0.0, 1e-12));
}

TEST_CASE("normalize", "[math]") {
    CHECK_THAT(length(normalize(Vec3d{3, -4, 12})), WithinAbs(1.0, 1e-15));
    const Vec3d zero = normalize(Vec3d{});
    CHECK(zero.x == 0.0); // no NaN for a zero vector
}

TEST_CASE("float loses precision at planetary scale, double does not", "[math][precision]") {
    // This test documents WHY the renderer is camera-relative.
    const double earthRadius = 6378137.0;
    const double step = 0.01; // 1 cm
    const auto f = static_cast<float>(earthRadius);
    const auto fStep = static_cast<float>(earthRadius + step);
    CHECK(f == fStep);                                      // float cannot see 1 cm here
    CHECK((earthRadius + step) - earthRadius > 0.009);      // double can

    const Vec3d relative = Vec3d{earthRadius + step, 0, 0} - Vec3d{earthRadius, 0, 0};
    CHECK_THAT(static_cast<double>(Vec3f(relative).x), WithinAbs(step, 1e-6)); // relative: fine
}
