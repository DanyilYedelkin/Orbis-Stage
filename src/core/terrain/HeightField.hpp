#pragma once

#include "core/math/Vec3.hpp"
#include "core/terrain/ElevationSource.hpp"
#include "core/terrain/Noise.hpp"

#include <cstdint>

namespace orbis {

/// Procedural planet: 3D noise on the sphere (no pole stretching or seam), continents, mountain
/// ranges, ridged relief and detail down to about a metre.
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
