#pragma once

#include "core/math/Vec3.hpp"
#include "core/terrain/ElevationSource.hpp"
#include "core/terrain/Noise.hpp"

#include <cstdint>

namespace orbis {

/// Procedural elevation model of a fictional planet.
///
/// Sampled by DIRECTION from the planet centre (a unit vector), never by (lat, lon):
/// 2D noise on lat/lon would be stretched near the poles and have a seam at 180 degrees;
/// 3D noise evaluated on the sphere has neither.
///
/// Layers: continents (thousands of km), where mountain ranges run (hundreds of km), alpine
/// ridged relief inside them (kilometres) and fractal detail down to ~1 m - which is what the LOD
/// quadtree reveals as the camera descends.
class HeightField final : public ElevationSource {
public:
    explicit HeightField(std::uint64_t seed);

    using ElevationSource::elevation;
    double elevation(const Vec3d& unitDirection, double sampleSpacing) const override;

    static constexpr double maxElevation() { return kMaxTerrainElevation; }

private:
    PerlinNoise continents_;
    PerlinNoise ranges_;
    PerlinNoise alpine_;
    PerlinNoise detail_;
};

} // namespace orbis
