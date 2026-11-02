#include "core/terrain/CubeSphere.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <vector>

using namespace orbis;
using Catch::Matchers::WithinAbs;

TEST_CASE("every face basis satisfies cross(u, v) == normal", "[cubesphere]") {
    for (const CubeFace face : kAllCubeFaces) {
        const FaceBasis& b = faceBasis(face);
        const Vec3d c = cross(b.uAxis, b.vAxis);
        CHECK(c.x == b.normal.x);
        CHECK(c.y == b.normal.y);
        CHECK(c.z == b.normal.z);
    }
}

TEST_CASE("cube-to-sphere output is unit length", "[cubesphere]") {
    for (const CubeFace face : kAllCubeFaces) {
        for (int j = 0; j <= 20; ++j) {
            for (int i = 0; i <= 20; ++i) {
                const Vec3d p = faceToSphere(face, -1.0 + i * 0.1, -1.0 + j * 0.1);
                REQUIRE_THAT(length(p), WithinAbs(1.0, 1e-15));
            }
        }
    }
}

TEST_CASE("face edges match their neighbours bit-for-bit (no cracks)", "[cubesphere]") {
    // Collect the border vertices of all 6 faces; each must coincide EXACTLY with a border
    // vertex of another face. Exact equality, not a tolerance: any difference is a crack.
    struct Sample {
        CubeFace face;
        Vec3d p;
    };
    std::vector<Sample> border;
    constexpr int n = 32;
    for (const CubeFace face : kAllCubeFaces) {
        for (int k = 0; k <= n; ++k) {
            const double t = (k == n) ? 1.0 : -1.0 + k * (2.0 / n);
            for (const auto& [u, v] : {std::pair{t, -1.0}, {t, 1.0}, {-1.0, t}, {1.0, t}}) {
                border.push_back({face, faceToSphere(face, u, v)});
            }
        }
    }

    for (const Sample& s : border) {
        const bool matched = std::any_of(border.begin(), border.end(), [&](const Sample& o) {
            return o.face != s.face && o.p.x == s.p.x && o.p.y == s.p.y && o.p.z == s.p.z;
        });
        REQUIRE(matched);
    }
}

TEST_CASE("spherified cube is far more uniform than normalize()", "[cubesphere]") {
    // Area of a small cell at the face centre vs at the corner. Naive normalisation gives a
    // ratio of 3*sqrt(3) ~ 5.2; the spherified mapping stays at 4/3.
    const auto cellArea = [](auto mapping, double u, double v) {
        constexpr double h = 1e-4;
        const Vec3d p0 = mapping(u, v);
        const Vec3d pu = mapping(u + h, v);
        const Vec3d pv = mapping(u, v + h);
        return length(cross(pu - p0, pv - p0));
    };
    const auto spherified = [](double u, double v) { return faceToSphere(CubeFace::PosZ, u, v); };
    const auto naive = [](double u, double v) {
        return normalize(faceToCube(CubeFace::PosZ, u, v));
    };

    const double naiveRatio = cellArea(naive, 0.0, 0.0) / cellArea(naive, 1.0 - 1e-3, 1.0 - 1e-3);
    const double sphRatio =
        cellArea(spherified, 0.0, 0.0) / cellArea(spherified, 1.0 - 1e-3, 1.0 - 1e-3);

    CHECK(naiveRatio > 5.0);
    CHECK(sphRatio < 1.34); // measured: exactly 4/3 over the whole face
}
