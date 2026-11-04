#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/terrain/ElevationGrid.hpp"
#include "core/terrain/ElevationSource.hpp"
#include "core/terrain/HighResolutionTerrain.hpp"
#include "core/terrain/Noise.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace orbis {

/// Real Earth: a measured elevation grid (ETOPO, ~18 km resolution) plus procedural detail
/// for everything finer than the data can show.
///
/// This "measured base + synthetic detail" split is how flight and military simulators fill the
/// gap between the available data and the resolution the viewer needs: the large shapes (coasts,
/// valleys, mountain ranges) are real, the detail is plausible and consistent. The detail is
/// band-limited to start exactly where the data stops, and it is scaled by the measured
/// elevation, so the Alps get rugged and the Netherlands stay flat.
/// Statistics of the synthetic mountain layers over the sphere (mean, standard deviation), used to
/// normalise them. Measured over 20 000 random directions; a test re-measures and compares.
inline constexpr FbmParams kErodedParams{.octaves = 8, .lacunarity = 2.0, .gain = 0.55};
inline constexpr FbmParams kCrestParams{
    .octaves = 6, .lacunarity = 2.0, .gain = 0.5, .crestSoftness = 0.15};
inline constexpr double kErodedMean = 0.0;
inline constexpr double kErodedStd = 0.091;
inline constexpr double kCrestMean = 0.762;
inline constexpr double kCrestStd = 0.134;

class EarthTerrain final : public ElevationSource {
public:
    /// `highResolution`: optional regional DEM (e.g. .hgt tiles of the Alps). Inside it, the
    /// real high-resolution relief replaces the base data and its synthetic mountains; a few km
    /// wide band at its edge blends between the two.
    EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed,
                 std::optional<ElevationGrid> highResolution = std::nullopt);

    /// Any high-resolution layer, e.g. tiles streamed on demand (HgtTileStreamer). Shared, so
    /// the application can drive the streaming while the terrain samples it.
    EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed,
                 std::shared_ptr<const HighResolutionTerrain> highResolution);

    using ElevationSource::elevation;
    double elevation(const Vec3d& unitDirection, double sampleSpacing) const override;

    /// The measured value alone at geodetic coordinates (radians), unfiltered. For tests/tools.
    double measuredElevation(double latitude, double longitude) const {
        return grid_.sample(latitude, longitude, 0.0);
    }

    /// The measured base grid (ETOPO), e.g. to tell the sea from lakes in the land cover map.
    const ElevationGrid& measuredGrid() const { return grid_; }

    bool hasHighResolution() const { return highResolution_ != nullptr; }

    /// Changes when streamed high-resolution data arrives or leaves (see ElevationSource).
    std::uint64_t version() const override {
        return highResolution_ ? highResolution_->version() : 0;
    }

private:
    double baseElevation(const Vec3d& dir, double latitude, double longitude,
                         double sampleSpacing) const;

    ElevationGrid grid_;
    ElevationGrid relief_; ///< Smooth local relief: where (and how strongly) the mountains are.
    Ellipsoid ellipsoid_;
    double dataSpacing_; ///< Metres between grid nodes at full resolution.
    PerlinNoise detail_;
    PerlinNoise alpine_;
    std::shared_ptr<const HighResolutionTerrain> highResolution_;
};

} // namespace orbis
