#pragma once

#include "core/math/Vec3.hpp"

namespace orbis {

/// Radians; height in metres above the ellipsoid.
struct Geodetic {
    double latitude = 0.0;
    double longitude = 0.0;
    double height = 0.0;
};

/// Unit vectors in ECEF.
struct EnuFrame {
    Vec3d east;
    Vec3d north;
    Vec3d up;
};

/// ECEF: +Z through the north pole, +X through (0, 0), +Y through (0, 90 E). Metres.
class Ellipsoid {
public:
    constexpr Ellipsoid(double semiMajorAxis, double flattening)
        : a_(semiMajorAxis), f_(flattening), b_(semiMajorAxis * (1.0 - flattening)),
          e2_(flattening * (2.0 - flattening)) {}

    static constexpr Ellipsoid wgs84() { return {6378137.0, 1.0 / 298.257223563}; }

    constexpr double semiMajorAxis() const { return a_; }
    constexpr double semiMinorAxis() const { return b_; }
    constexpr double flattening() const { return f_; }
    constexpr double eccentricitySquared() const { return e2_; }

    Vec3d geodeticToEcef(const Geodetic& g) const;

    /// Iterative; sub-micrometre after a few steps, stable at the poles.
    Geodetic ecefToGeodetic(const Vec3d& p) const;

    /// For a point on the surface.
    Vec3d surfaceNormal(const Vec3d& surfacePoint) const;

    /// Hit of the ray from the centre.
    Vec3d surfacePointAlong(const Vec3d& direction) const;

    static EnuFrame enuFrame(double latitude, double longitude);

private:
    double primeVerticalRadius(double sinLatitude) const;

    double a_;
    double f_;
    double b_;
    double e2_;
};

} // namespace orbis
