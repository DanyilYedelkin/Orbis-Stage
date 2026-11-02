#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/terrain/ElevationGrid.hpp"
#include "core/terrain/ElevationSource.hpp"
#include "core/terrain/Noise.hpp"

#include <cstdint>

namespace orbis {

/// Real Earth: a measured elevation grid (ETOPO, ~18 km resolution) plus procedural detail
/// for everything finer than the data can show.
///
/// This "measured base + synthetic detail" split is how flight and military simulators fill the
/// gap between the available data and the resolution the viewer needs: the large shapes (coasts,
/// valleys, mountain ranges) are real, the detail is plausible and consistent. The detail is
/// band-limited to start exactly where the data stops, and it is scaled by the measured
/// elevation, so the Alps get rugged and the Netherlands stay flat.
class EarthTerrain final : public ElevationSource {
public:
    EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed);

    using ElevationSource::elevation;
    double elevation(const Vec3d& unitDirection, double sampleSpacing) const override;

    /// The measured value alone at geodetic coordinates (radians), unfiltered. For tests/tools.
    double measuredElevation(double latitude, double longitude) const {
        return grid_.sample(latitude, longitude, 0.0);
    }

private:
    ElevationGrid grid_;
    ElevationGrid relief_; ///< Smooth local relief: where (and how strongly) the mountains are.
    Ellipsoid ellipsoid_;
    double dataSpacing_; ///< Metres between grid nodes at full resolution.
    PerlinNoise detail_;
    PerlinNoise alpine_;
};

} // namespace orbis
