#pragma once

#include "core/math/Vec3.hpp"

namespace orbis {

/// Geodetic coordinates: angles in RADIANS, height in metres above the ellipsoid surface.
/// (Geodetic latitude is the angle between the equator and the surface NORMAL - this is what
/// GPS and maps use - not the angle to the planet centre.)
struct Geodetic {
    double latitude = 0.0;
    double longitude = 0.0;
    double height = 0.0;
};

/// Local tangent frame at a point: East, North, Up. Unit vectors expressed in ECEF.
/// The standard local frame in simulation / navigation (aviation often uses NED instead).
struct EnuFrame {
    Vec3d east;
    Vec3d north;
    Vec3d up;
};

/// Oblate ellipsoid of revolution in an Earth-Centred, Earth-Fixed (ECEF) frame:
/// origin at the planet centre, +Z through the North Pole, +X through (lat 0, lon 0),
/// +Y through (lat 0, lon 90 E). Right-handed, metres.
class Ellipsoid {
public:
    constexpr Ellipsoid(double semiMajorAxis, double flattening)
        : a_(semiMajorAxis), f_(flattening), b_(semiMajorAxis * (1.0 - flattening)),
          e2_(flattening * (2.0 - flattening)) {}

    /// The datum used by GPS.
    static constexpr Ellipsoid wgs84() { return {6378137.0, 1.0 / 298.257223563}; }

    constexpr double semiMajorAxis() const { return a_; }        ///< Equatorial radius a.
    constexpr double semiMinorAxis() const { return b_; }        ///< Polar radius b.
    constexpr double flattening() const { return f_; }           ///< f = (a - b) / a.
    constexpr double eccentricitySquared() const { return e2_; } ///< e^2 = f (2 - f).

    /// Exact closed form.
    Vec3d geodeticToEcef(const Geodetic& g) const;

    /// Inverse has no closed form that is both simple and stable; we iterate. Converges to
    /// sub-micrometre accuracy in a few steps and stays stable at the poles.
    Geodetic ecefToGeodetic(const Vec3d& p) const;

    /// Outward unit normal for a point ON the surface (gradient of x^2/a^2 + y^2/a^2 + z^2/b^2).
    Vec3d surfaceNormal(const Vec3d& surfacePoint) const;

    /// Where a ray from the centre in `direction` hits the surface. Used to "inflate" the unit
    /// cube-sphere onto the real ellipsoid.
    Vec3d surfacePointAlong(const Vec3d& direction) const;

    static EnuFrame enuFrame(double latitude, double longitude);

private:
    /// Radius of curvature in the prime vertical, N(lat).
    double primeVerticalRadius(double sinLatitude) const;

    double a_;
    double f_;
    double b_;
    double e2_;
};

} // namespace orbis
