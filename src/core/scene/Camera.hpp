#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Mat4.hpp"
#include "core/math/Vec3.hpp"

namespace orbis {

struct CameraBasis {
    Vec3d forward;
    Vec3d right;
    Vec3d up;
    EnuFrame enu;      ///< Local East-North-Up frame at the camera position.
    Geodetic geodetic; ///< Camera position as latitude / longitude / altitude.
};

/// Free-flight camera that keeps the horizon level anywhere on the planet.
///
/// Orientation is stored as heading + pitch RELATIVE TO THE LOCAL ENU FRAME, not as a world
/// rotation. "Up" therefore always means "away from the ground" - over Slovakia, over the
/// South Pole, upside-down from the point of view of the ECEF axes - exactly like a pilot sees it.
class Camera {
public:
    explicit Camera(const Ellipsoid& ellipsoid) : ellipsoid_(ellipsoid) {}

    void setPosition(const Geodetic& g);
    const Vec3d& position() const { return position_; }

    double heading() const { return heading_; } ///< Radians, 0 = north, +pi/2 = east.
    double pitch() const { return pitch_; }     ///< Radians, + = up, clamped to +-89 degrees.
    void setOrientation(double heading, double pitch);
    void rotate(double deltaHeading, double deltaPitch);

    /// Moves by `delta` (ECEF metres) and keeps the camera above `minAltitude`.
    ///
    /// The look direction is preserved in WORLD space and re-expressed in the new local frame.
    /// Without this, flying over a pole would spin the view by 180 degrees, because "north"
    /// suddenly points behind you.
    void translate(const Vec3d& delta);

    CameraBasis basis() const;

    /// Rotation-only view matrix (the renderer is camera-relative, see Mat4.hpp).
    Mat4f viewRotationMatrix() const;
    Mat4f projectionMatrix(float aspect) const;

    /// 45 degrees: close to how big things look to the eye. 60+ is a wide-angle lens that
    /// visibly shrinks and pushes back distant mountains.
    float verticalFov = 0.7853982f;
    float nearPlane = 0.5f;         ///< Metres. Reversed-Z allows a tiny near plane.
    double minAltitude = 10.0;      ///< Metres above the ellipsoid.

private:
    void lookAlong(const Vec3d& worldDirection);

    Ellipsoid ellipsoid_;
    Vec3d position_{ellipsoid_.semiMajorAxis() * 2.0, 0.0, 0.0};
    double heading_ = 0.0;
    double pitch_ = 0.0;
};

} // namespace orbis
