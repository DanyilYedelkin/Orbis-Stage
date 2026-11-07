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

/// Mean / std of the synthetic mountain layers over the sphere, for normalisation. Measured over
/// 20 000 directions; a test re-measures them.
inline constexpr FbmParams kErodedParams{.octaves = 8, .lacunarity = 2.0, .gain = 0.55};
inline constexpr FbmParams kCrestParams{
    .octaves = 6, .lacunarity = 2.0, .gain = 0.5, .crestSoftness = 0.15};
inline constexpr double kErodedMean = 0.0;
inline constexpr double kErodedStd = 0.091;
inline constexpr double kCrestMean = 0.762;
inline constexpr double kCrestStd = 0.134;

/// Measured elevation (ETOPO1 10' or ETOPO 2022 1') plus synthetic detail below its resolution,
/// band-limited to start where the data stops and scaled by the measured local relief.
/// Optionally a high-resolution layer (.hgt tiles) replaces both where it has data.
class EarthTerrain final : public ElevationSource {
public:
    /// `highResolution`: a static regional DEM.
    EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed,
                 std::optional<ElevationGrid> highResolution = std::nullopt);

    /// Shared so the application can drive streaming while workers sample it.
    EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed,
                 std::shared_ptr<const HighResolutionTerrain> highResolution);

    using ElevationSource::elevation;
    double elevation(const Vec3d& unitDirection, double sampleSpacing) const override;

    /// Unfiltered measured value at geodetic radians.
    double measuredElevation(double latitude, double longitude) const {
        return grid_.sample(latitude, longitude, 0.0);
    }

    const ElevationGrid& measuredGrid() const { return grid_; }

    bool hasHighResolution() const { return highResolution_ != nullptr; }

    std::uint64_t version() const override {
        return highResolution_ ? highResolution_->version() : 0;
    }

private:
    double baseElevation(const Vec3d& dir, double latitude, double longitude,
                         double sampleSpacing) const;

    ElevationGrid grid_;
    double dataSpacing_; ///< Metres.
    /// The relief map is built at the grid level nearest the 10' its thresholds were set at.
    int reliefBase_;
    ElevationGrid relief_; ///< |data - smoothed data|: where, and how strongly, it is mountainous.
    Ellipsoid ellipsoid_;
    /// Synthetic peaks 8 km apart under 10' data; under finer data they start one octave above
    /// its Nyquist wavelength, so they do not compete with real valleys, and shrink like a
    /// fractal surface.
    double alpineWavelength_;
    double alpineScale_;
    double roughnessScale_;
    PerlinNoise detail_;
    PerlinNoise alpine_;
    std::shared_ptr<const HighResolutionTerrain> highResolution_;
};

} // namespace orbis
