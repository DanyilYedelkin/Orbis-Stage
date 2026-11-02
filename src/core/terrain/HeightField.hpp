#pragma once

#include "core/math/Vec3.hpp"
#include "core/terrain/Noise.hpp"

#include <cstdint>

namespace orbis {

/// Procedural elevation model of the planet.
///
/// Sampled by DIRECTION from the planet centre (a unit vector), never by (lat, lon):
/// 2D noise on lat/lon would be stretched near the poles and have a seam at 180 degrees;
/// 3D noise evaluated on the sphere has neither.
///
/// Three layers: continents (thousands of km), mountain ridges (tens of km) and fractal detail
/// down to ~1 m, which is what the LOD quadtree reveals as the camera descends.
class HeightField {
public:
    explicit HeightField(std::uint64_t seed);

    /// Elevation in metres relative to the ellipsoid. Sea level = 0, negative = ocean floor.
    ///
    /// `sampleSpacing` (metres) is the distance between the mesh vertices that will use this
    /// value. Features smaller than ~2 samples cannot be represented (Nyquist), so they are
    /// filtered out instead of aliasing. 0 = full detail.
    double elevation(const Vec3d& unitDirection, double sampleSpacing = 0.0) const;

    /// Conservative upper bound of elevation(), used for bounding volumes (verified by a test).
    static constexpr double maxElevation() { return 11000.0; }

private:
    PerlinNoise continents_;
    PerlinNoise ranges_;
    PerlinNoise alpine_;
    PerlinNoise detail_;
};

} // namespace orbis
