#include "core/geo/Ellipsoid.hpp"

#include <cmath>

namespace orbis {

double Ellipsoid::primeVerticalRadius(double sinLatitude) const {
    return a_ / std::sqrt(1.0 - e2_ * sinLatitude * sinLatitude);
}

Vec3d Ellipsoid::geodeticToEcef(const Geodetic& g) const {
    const double sinLat = std::sin(g.latitude);
    const double cosLat = std::cos(g.latitude);
    const double n = primeVerticalRadius(sinLat);
    const double r = (n + g.height) * cosLat; // distance from the polar axis
    return {r * std::cos(g.longitude), r * std::sin(g.longitude),
            (n * (1.0 - e2_) + g.height) * sinLat};
}

Geodetic Ellipsoid::ecefToGeodetic(const Vec3d& pt) const {
    const double p = std::sqrt(pt.x * pt.x + pt.y * pt.y); // distance from the polar axis
    const double longitude = std::atan2(pt.y, pt.x);

    // Fixed-point iteration on   tan(lat) = (z + e^2 N sin(lat)) / p.
    // Initial guess = the answer for a point on the surface, so it is already close.
    // atan2 (not atan) keeps it well defined at the poles where p == 0.
    double latitude = std::atan2(pt.z, p * (1.0 - e2_));
    for (int i = 0; i < 10; ++i) {
        const double sinLat = std::sin(latitude);
        const double n = primeVerticalRadius(sinLat);
        const double next = std::atan2(pt.z + e2_ * n * sinLat, p);
        const bool converged = std::abs(next - latitude) < 1e-15;
        latitude = next;
        if (converged) {
            break;
        }
    }

    // Height formula without the usual  p / cos(lat) - N,  which divides by ~0 at the poles:
    //   h = p cos(lat) + (z + e^2 N sin(lat)) sin(lat) - N
    const double sinLat = std::sin(latitude);
    const double cosLat = std::cos(latitude);
    const double n = primeVerticalRadius(sinLat);
    const double height = p * cosLat + (pt.z + e2_ * n * sinLat) * sinLat - n;

    return {latitude, longitude, height};
}

Vec3d Ellipsoid::surfaceNormal(const Vec3d& surfacePoint) const {
    const double invA2 = 1.0 / (a_ * a_);
    const double invB2 = 1.0 / (b_ * b_);
    return normalize(
        Vec3d{surfacePoint.x * invA2, surfacePoint.y * invA2, surfacePoint.z * invB2});
}

Vec3d Ellipsoid::surfacePointAlong(const Vec3d& direction) const {
    // Solve |t*d| on the ellipsoid:  t^2 (dx^2/a^2 + dy^2/a^2 + dz^2/b^2) = 1.
    const double k = (direction.x * direction.x + direction.y * direction.y) / (a_ * a_) +
                     (direction.z * direction.z) / (b_ * b_);
    return direction / std::sqrt(k);
}

EnuFrame Ellipsoid::enuFrame(double latitude, double longitude) {
    const double sinLat = std::sin(latitude);
    const double cosLat = std::cos(latitude);
    const double sinLon = std::sin(longitude);
    const double cosLon = std::cos(longitude);
    return {
        .east = {-sinLon, cosLon, 0.0},
        .north = {-sinLat * cosLon, -sinLat * sinLon, cosLat},
        .up = {cosLat * cosLon, cosLat * sinLon, sinLat},
    };
}

} // namespace orbis
