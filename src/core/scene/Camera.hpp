#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Mat4.hpp"
#include "core/math/Vec3.hpp"

namespace orbis {

struct CameraBasis {
    Vec3d forward;
    Vec3d right;
    Vec3d up;
    EnuFrame enu;
    Geodetic geodetic;
};

/// Heading and pitch relative to the local ENU frame, so the horizon stays level anywhere.
class Camera {
public:
    explicit Camera(const Ellipsoid& ellipsoid) : ellipsoid_(ellipsoid) {}

    void setPosition(const Geodetic& g);
    const Vec3d& position() const { return position_; }

    double heading() const { return heading_; } ///< Radians, clockwise from north.
    double pitch() const { return pitch_; }     ///< Radians, clamped to +-89 degrees.
    void setOrientation(double heading, double pitch);
    void rotate(double deltaHeading, double deltaPitch);

    /// Keeps the look direction fixed in ECEF; otherwise crossing a pole flips the view.
    void translate(const Vec3d& delta);

    CameraBasis basis() const;

    Mat4f viewRotationMatrix() const;
    Mat4f projectionMatrix(float aspect) const;

    float verticalFov = 0.7853982f;
    float nearPlane = 0.5f;
    double minAltitude = 10.0;

private:
    void lookAlong(const Vec3d& worldDirection);

    Ellipsoid ellipsoid_;
    Vec3d position_{ellipsoid_.semiMajorAxis() * 2.0, 0.0, 0.0};
    double heading_ = 0.0;
    double pitch_ = 0.0;
};

} // namespace orbis
