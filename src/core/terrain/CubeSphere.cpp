#include "core/terrain/CubeSphere.hpp"

#include <cmath>
#include <cstddef>

namespace orbis {

namespace {

// Each row was chosen so that cross(u, v) == normal (verified by a unit test).
constexpr std::array<FaceBasis, 6> kFaceBases = {{
    {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},   // +X
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  // -X
    {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},   // +Y
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},  // -Y
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   // +Z
    {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},  // -Z
}};

} // namespace

const FaceBasis& faceBasis(CubeFace face) {
    return kFaceBases[static_cast<std::size_t>(face)];
}

Vec3d faceToCube(CubeFace face, double u, double v) {
    const FaceBasis& b = faceBasis(face);
    return b.normal + b.uAxis * u + b.vAxis * v;
}

Vec3d cubeToSphere(const Vec3d& c) {
    const double x2 = c.x * c.x;
    const double y2 = c.y * c.y;
    const double z2 = c.z * c.z;
    // For any point on the cube surface the result has length exactly 1 (algebraically);
    // the final normalize() only removes floating-point rounding.
    const Vec3d s{
        c.x * std::sqrt(1.0 - y2 / 2.0 - z2 / 2.0 + y2 * z2 / 3.0),
        c.y * std::sqrt(1.0 - z2 / 2.0 - x2 / 2.0 + z2 * x2 / 3.0),
        c.z * std::sqrt(1.0 - x2 / 2.0 - y2 / 2.0 + x2 * y2 / 3.0),
    };
    return normalize(s);
}

} // namespace orbis
