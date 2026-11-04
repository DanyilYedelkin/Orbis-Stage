#pragma once

#include "core/terrain/ElevationGrid.hpp"

#include <cstdint>
#include <optional>

namespace orbis {

/// Regional high-resolution elevation that EarthTerrain blends over its global base layer.
///
/// Two implementations: a static mosaic loaded at startup (HgtMosaicTerrain) and tiles streamed
/// on demand around the camera (HgtTileStreamer). Must be thread-safe for concurrent sample()
/// calls from the mesh worker threads.
class HighResolutionTerrain {
public:
    struct Sample {
        double height;  ///< Real elevation, metres.
        double weight;  ///< 0..1: how much of it to use (blends into the base near any boundary).
        double spacing; ///< Metres between the data's nodes (for the synthetic detail below it).
    };

    virtual ~HighResolutionTerrain() = default;

    /// At geodetic latitude / longitude (radians), pre-filtered for `sampleSpacing` metres (0 =
    /// full detail). nullopt where there is no data.
    virtual std::optional<Sample> sample(double latitude, double longitude,
                                         double sampleSpacing) const = 0;

    /// Increases whenever the data changes (tiles streamed in or out). Meshes built from an older
    /// version near a change are stale. Static data: always 0.
    virtual std::uint64_t version() const { return 0; }

protected:
    HighResolutionTerrain() = default;
    HighResolutionTerrain(const HighResolutionTerrain&) = default;
    HighResolutionTerrain& operator=(const HighResolutionTerrain&) = default;
};

/// One regional grid loaded up front (e.g. all .hgt tiles of data/hgt stitched by
/// loadHgtMosaic). Blends out near ANY boundary of the data - the outer edge, a missing tile, a
/// void - using a blurred coverage mask and a feather band at the edge.
class HgtMosaicTerrain final : public HighResolutionTerrain {
public:
    explicit HgtMosaicTerrain(ElevationGrid grid);

    std::optional<Sample> sample(double latitude, double longitude,
                                 double sampleSpacing) const override;

private:
    ElevationGrid grid_;
    ElevationGrid coverage_; ///< validityMask() of grid_, read blurred.
    double spacing_;
};

/// Shared by both implementations: metres over which high-resolution data blends into the base.
inline constexpr double kHighResolutionFeather = 4000.0;

} // namespace orbis
