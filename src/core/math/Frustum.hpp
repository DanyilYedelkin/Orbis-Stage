#pragma once

#include "core/math/Vec3.hpp"

#include <array>
#include <cmath>

namespace orbis {

/// Normal points inwards.
struct Plane {
    Vec3d normal;
    double distance = 0.0;

    double signedDistance(const Vec3d& p) const { return dot(normal, p) + distance; }
};

/// Camera-relative. No far plane: horizon culling does that job.
class Frustum {
public:
    static Frustum fromCamera(const Vec3d& forward, const Vec3d& right, const Vec3d& up,
                              double verticalFov, double aspect, double nearPlane);

    /// Conservative: false only if the sphere is entirely outside one plane.
    bool intersectsSphere(const Vec3d& center, double radius) const {
        for (const Plane& plane : planes_) {
            if (plane.signedDistance(center) < -radius) {
                return false;
            }
        }
        return true;
    }

    const std::array<Plane, 5>& planes() const { return planes_; }

private:
    std::array<Plane, 5> planes_{}; ///< left, right, bottom, top, near
};

inline Frustum Frustum::fromCamera(const Vec3d& forward, const Vec3d& right, const Vec3d& up,
                                   double verticalFov, double aspect, double nearPlane) {
    const double tanY = std::tan(verticalFov / 2.0);
    const double tanX = tanY * aspect;

    Frustum f;
    f.planes_[0] = {normalize(right + forward * tanX), 0.0};  // left
    f.planes_[1] = {normalize(-right + forward * tanX), 0.0}; // right
    f.planes_[2] = {normalize(up + forward * tanY), 0.0};     // bottom
    f.planes_[3] = {normalize(-up + forward * tanY), 0.0};    // top
    f.planes_[4] = {forward, -nearPlane};                     // near
    return f;
}

} // namespace orbis
