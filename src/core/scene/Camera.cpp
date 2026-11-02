#include "core/scene/Camera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace orbis {

namespace {
constexpr double kMaxPitch = 89.0 * std::numbers::pi / 180.0;
}

void Camera::setPosition(const Geodetic& g) {
    Geodetic clamped = g;
    clamped.height = std::max(clamped.height, minAltitude);
    position_ = ellipsoid_.geodeticToEcef(clamped);
}

void Camera::setOrientation(double heading, double pitch) {
    // Keep heading in [-pi, pi] so it never grows without bound (and loses precision).
    heading_ = std::remainder(heading, 2.0 * std::numbers::pi);
    // +-90 degrees exactly would make "right" undefined (forward parallel to up).
    pitch_ = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

void Camera::rotate(double deltaHeading, double deltaPitch) {
    setOrientation(heading_ + deltaHeading, pitch_ + deltaPitch);
}

void Camera::translate(const Vec3d& delta) {
    const Vec3d forward = basis().forward;

    position_ += delta;
    Geodetic g = ellipsoid_.ecefToGeodetic(position_);
    if (g.height < minAltitude) {
        g.height = minAltitude;
        position_ = ellipsoid_.geodeticToEcef(g);
    }

    lookAlong(forward);
}

void Camera::lookAlong(const Vec3d& worldDirection) {
    const Geodetic g = ellipsoid_.ecefToGeodetic(position_);
    const EnuFrame enu = Ellipsoid::enuFrame(g.latitude, g.longitude);
    const Vec3d d = normalize(worldDirection);

    const double pitch = std::asin(std::clamp(dot(d, enu.up), -1.0, 1.0));
    const double heading = std::atan2(dot(d, enu.east), dot(d, enu.north));
    setOrientation(heading, pitch);
}

CameraBasis Camera::basis() const {
    CameraBasis b;
    b.geodetic = ellipsoid_.ecefToGeodetic(position_);
    b.enu = Ellipsoid::enuFrame(b.geodetic.latitude, b.geodetic.longitude);

    const double cosPitch = std::cos(pitch_);
    const double sinPitch = std::sin(pitch_);
    const double cosHeading = std::cos(heading_);
    const double sinHeading = std::sin(heading_);

    // Horizontal heading direction, tilted up/down by pitch.
    const Vec3d horizontal = b.enu.north * cosHeading + b.enu.east * sinHeading;
    b.forward = horizontal * cosPitch + b.enu.up * sinPitch;
    // "Right" stays horizontal (no roll): the heading direction rotated 90 degrees clockwise.
    b.right = b.enu.east * cosHeading - b.enu.north * sinHeading;
    b.up = cross(b.right, b.forward);
    return b;
}

Mat4f Camera::viewRotationMatrix() const {
    const CameraBasis b = basis();
    // Rotation values are in [-1, 1]: converting them to float loses nothing that matters.
    return viewRotation(Vec3f(b.right), Vec3f(b.up), Vec3f(b.forward));
}

Mat4f Camera::projectionMatrix(float aspect) const {
    return perspectiveReversedZInfinite(verticalFov, aspect, nearPlane);
}

} // namespace orbis
