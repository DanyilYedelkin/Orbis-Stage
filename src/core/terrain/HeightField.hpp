#pragma once

#include "core/math/Vec3.hpp"
#include "core/terrain/Noise.hpp"

#include <cstdint>

namespace orbis {

/// Procedural elevation model of the planet.
///
/// Sampled by DIRECTION from the planet centre (a unit vector), never by (lat, lon):
/// 2D noise on lat/lon would be stretched near the poles and have a seam at 180 degrees;
/// 3D noise evaluated on the sphere has neither. Stage 4 will add real DEM data (SRTM) behind
/// the same interface.
class HeightField {
public:
    explicit HeightField(std::uint64_t seed);

    /// Elevation in metres relative to the ellipsoid. Sea level = 0, negative = ocean floor.
    double elevation(const Vec3d& unitDirection) const;

private:
    PerlinNoise continents_;
    PerlinNoise mountains_;
};

} // namespace orbis
