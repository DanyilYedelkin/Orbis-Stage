#pragma once

#include "core/math/Vec3.hpp"

#include <cstdint>

namespace orbis {

/// Bounds are built from it before any mesh exists; every source is tested against it.
inline constexpr double kMaxTerrainElevation = 12000.0;

/// Called concurrently from the mesh workers.
class ElevationSource {
public:
    virtual ~ElevationSource() = default;

    /// Metres above the ellipsoid. Detail finer than two `sampleSpacing` is filtered out
    /// (0 = full detail).
    virtual double elevation(const Vec3d& unitDirection, double sampleSpacing) const = 0;

    double elevation(const Vec3d& unitDirection) const { return elevation(unitDirection, 0.0); }

    /// Bumped when the data changes; meshes from an older version may be stale.
    virtual std::uint64_t version() const { return 0; }

protected:
    ElevationSource() = default;
    ElevationSource(const ElevationSource&) = default;
    ElevationSource& operator=(const ElevationSource&) = default;
};

} // namespace orbis
