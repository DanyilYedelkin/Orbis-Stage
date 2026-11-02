#pragma once

#include "core/math/Vec3.hpp"

namespace orbis {

/// Upper bound of terrain elevation for EVERY source, in metres. Bounding volumes are built from
/// it before any mesh exists, so each source has a unit test proving it never exceeds it.
inline constexpr double kMaxTerrainElevation = 11000.0;

/// Anything that can answer "how high is the ground in this direction?".
///
/// The quadtree, the mesh generator and the worker threads only know this interface, so a
/// procedural planet and real Earth data are interchangeable. Implementations must be
/// thread-safe for concurrent calls (in practice: immutable after construction).
class ElevationSource {
public:
    virtual ~ElevationSource() = default;

    /// Elevation in metres relative to the ellipsoid (sea level = 0, negative = ocean floor) for
    /// a unit direction from the planet centre. `sampleSpacing` (metres) is the distance between
    /// the mesh vertices that will use the value: detail smaller than ~2 samples must be filtered
    /// out (Nyquist) instead of aliasing. 0 = full detail.
    virtual double elevation(const Vec3d& unitDirection, double sampleSpacing) const = 0;

    double elevation(const Vec3d& unitDirection) const { return elevation(unitDirection, 0.0); }

protected:
    ElevationSource() = default;
    ElevationSource(const ElevationSource&) = default;
    ElevationSource& operator=(const ElevationSource&) = default;
};

} // namespace orbis
