#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Vec3.hpp"

#include <cmath>

namespace orbis {

/// Conservative: the sphere's nearest point must be farther than the sum of the tangent lengths
/// from the camera and from the sphere's highest point. The occluder must lie inside the terrain.
inline bool isBelowHorizon(const Vec3d& camera, const Vec3d& center, double radius,
                           double occluderRadius) {
    const double r2 = occluderRadius * occluderRadius;
    const double cameraDistSq = lengthSquared(camera);
    if (cameraDistSq <= r2) {
        return false;
    }
    const double cameraTangent = std::sqrt(cameraDistSq - r2);

    const double top = length(center) + radius;
    if (top * top <= r2) {
        return true;
    }
    const double objectTangent = std::sqrt(top * top - r2);

    return distance(camera, center) - radius > cameraTangent + objectTangent;
}

/// Against the ellipsoid: scaling by (1/a, 1/a, 1/b) maps it to the unit sphere and keeps lines
/// of sight straight; radius / b encloses the scaled bounding sphere.
inline bool isBelowHorizon(const Vec3d& camera, const Vec3d& center, double radius,
                           const Ellipsoid& ellipsoid) {
    const double ia = 1.0 / ellipsoid.semiMajorAxis();
    const double ib = 1.0 / ellipsoid.semiMinorAxis();
    const auto scaled = [ia, ib](const Vec3d& p) { return Vec3d{p.x * ia, p.y * ia, p.z * ib}; };
    return isBelowHorizon(scaled(camera), scaled(center), radius * ib, 1.0);
}

} // namespace orbis
