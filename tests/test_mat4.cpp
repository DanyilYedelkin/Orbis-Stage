#include "core/math/Mat4.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
double depthOf(const Mat4d& proj, double zView) {
    const Vec4<double> clip = proj * Vec4<double>{0.0, 0.0, zView, 1.0};
    return clip.z / clip.w;
}
} // namespace

TEST_CASE("identity is neutral", "[math]") {
    Mat4d m = Mat4d::identity();
    m(3, 0) = 5.0;
    const Mat4d r = Mat4d::identity() * m;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            CHECK(r(c, row) == m(c, row));
        }
    }
}

TEST_CASE("reversed-Z infinite projection", "[math][projection]") {
    const double zNear = 0.5;
    const Mat4d proj = perspectiveReversedZInfinite(1.0, 16.0 / 9.0, zNear);

    CHECK_THAT(depthOf(proj, -zNear), WithinAbs(1.0, 1e-12)); // near plane -> 1
    CHECK(depthOf(proj, -1.0e9) > 0.0);                       // very far -> still > 0
    CHECK(depthOf(proj, -1.0e9) < 1e-9);                      // ... and close to 0

    // Strictly decreasing with distance, so glDepthFunc(GL_GREATER) keeps the nearest surface.
    double previous = 2.0;
    for (double d = zNear; d < 1.0e8; d *= 3.0) {
        const double depth = depthOf(proj, -d);
        CHECK(depth < previous);
        previous = depth;
    }
}

TEST_CASE("view rotation maps camera forward to -Z", "[math]") {
    const Vec3d right{0, 1, 0};
    const Vec3d up{0, 0, 1};
    const Vec3d forward{1, 0, 0};
    const Mat4d view = viewRotation(right, up, forward);

    const Vec4<double> f = view * Vec4<double>{forward.x, forward.y, forward.z, 0.0};
    CHECK_THAT(f.z, WithinAbs(-1.0, 1e-15));
    const Vec4<double> r = view * Vec4<double>{right.x, right.y, right.z, 0.0};
    CHECK_THAT(r.x, WithinAbs(1.0, 1e-15));
}
