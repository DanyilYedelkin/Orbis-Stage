#pragma once

#include "core/terrain/ElevationGrid.hpp"

#include <cstdint>
#include <optional>

namespace orbis {

/// Regional data EarthTerrain blends over its global grid. sample() is called concurrently from
/// the mesh workers.
class HighResolutionTerrain {
public:
    struct Sample {
        double height;
        double weight;  ///< 0..1, fades towards the edges of the data.
        double spacing; ///< Of the data, metres.
    };

    virtual ~HighResolutionTerrain() = default;

    /// Geodetic radians, filtered for `sampleSpacing` metres (0 = full detail).
    virtual std::optional<Sample> sample(double latitude, double longitude,
                                         double sampleSpacing) const = 0;

    virtual std::uint64_t version() const { return 0; }

protected:
    HighResolutionTerrain() = default;
    HighResolutionTerrain(const HighResolutionTerrain&) = default;
    HighResolutionTerrain& operator=(const HighResolutionTerrain&) = default;
};

/// A mosaic loaded up front. Fades out at its edge, missing tiles and voids through a blurred
/// coverage mask.
class HgtMosaicTerrain final : public HighResolutionTerrain {
public:
    explicit HgtMosaicTerrain(ElevationGrid grid);

    std::optional<Sample> sample(double latitude, double longitude,
                                 double sampleSpacing) const override;

private:
    ElevationGrid grid_;
    ElevationGrid coverage_;
    double spacing_;
};

/// Metres.
inline constexpr double kHighResolutionFeather = 4000.0;

} // namespace orbis
