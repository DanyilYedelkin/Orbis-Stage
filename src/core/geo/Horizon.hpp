#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Vec3.hpp"

#include <cmath>

namespace orbis {

/// True if the sphere (center, radius) is certainly hidden behind a planet modelled as an
/// occluding sphere of `occluderRadius` centred at the origin. All positions in ECEF.
///
/// Two points at distances r1, r2 from the centre can see each other only if they are no
/// further apart than the sum of their tangent lengths  sqrt(r1^2 - R^2) + sqrt(r2^2 - R^2)
/// (the line of sight that just grazes the planet). For a whole sphere we take its nearest
/// possible point and its highest possible point - conservative, never culls anything visible.
///
/// Use an occluder radius that lies INSIDE the real terrain everywhere (e.g. the ellipsoid's
/// semi-minor axis), otherwise visible terrain could be culled.
inline bool isBelowHorizon(const Vec3d& camera, const Vec3d& center, double radius,
                           double occluderRadius) {
    const double r2 = occluderRadius * occluderRadius;
    const double cameraDistSq = lengthSquared(camera);
    if (cameraDistSq <= r2) {
        return false; // camera inside the occluder: no meaningful horizon
    }
    const double cameraTangent = std::sqrt(cameraDistSq - r2);

    const double top = length(center) + radius;
    if (top * top <= r2) {
        return true; // the whole object is inside the occluder
    }
    const double objectTangent = std::sqrt(top * top - r2);

    return distance(camera, center) - radius > cameraTangent + objectTangent;
}

/// Same test against the ELLIPSOID itself (exact horizon, not a smaller inscribed sphere).
///
/// Trick: scale space by (1/a, 1/a, 1/b). The ellipsoid becomes the unit sphere, and since the
/// transform is linear, straight lines of sight stay straight - "is the line of sight blocked?"
/// has the same answer in both spaces. The bounding sphere becomes an ellipsoid; enclosing it in
/// a sphere of radius r / b (the largest scale factor) keeps the test conservative.
/// Valid occluder as long as terrain never goes below the ellipsoid (oceans are clamped to 0).
inline bool isBelowHorizon(const Vec3d& camera, const Vec3d& center, double radius,
                           const Ellipsoid& ellipsoid) {
    const double ia = 1.0 / ellipsoid.semiMajorAxis();
    const double ib = 1.0 / ellipsoid.semiMinorAxis();
    const auto scaled = [ia, ib](const Vec3d& p) { return Vec3d{p.x * ia, p.y * ia, p.z * ib}; };
    return isBelowHorizon(scaled(camera), scaled(center), radius * ib, 1.0);
}

} // namespace orbis
