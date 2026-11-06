#pragma once

#include "core/terrain/ElevationGrid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace orbis {

/// IGBP classes as in MODIS MCD12C1, plus InlandWater (see LandCover::separateInlandWater).
enum class LandClass : std::uint8_t {
    Ocean = 0, ///< MODIS "water"; only the sea after separateInlandWater().
    EvergreenNeedleleafForest = 1,
    EvergreenBroadleafForest = 2,
    DeciduousNeedleleafForest = 3,
    DeciduousBroadleafForest = 4,
    MixedForest = 5,
    ClosedShrubland = 6,
    OpenShrubland = 7,
    WoodySavanna = 8,
    Savanna = 9,
    Grassland = 10,
    PermanentWetland = 11,
    Cropland = 12,
    Urban = 13,
    CroplandMosaic = 14,
    SnowAndIce = 15,
    Barren = 16,
    InlandWater = 17,
};

inline constexpr int kLandClassCount = 18;
inline constexpr int kModisClassCount = 17; ///< In the .olc file.

std::string_view landClassName(LandClass c);

/// Global land cover, one class per cell. Cell-registered (cell (0, 0) spans from 90 N, 180 W),
/// unlike the node-registered elevation grids. Classes are categorical and never interpolated.
///
/// ".olc": char[4] "OLC1" | uint32 width, height, classCount | run-length rows (uint8 class,
/// LEB128 length) | uint32 FNV-1a of the cells. Written by tools/prepare_landcover.py.
class LandCover {
public:
    /// Throws std::runtime_error on any format problem.
    static LandCover load(const std::filesystem::path& path);

    /// Row-major, north to south.
    LandCover(int width, int height, std::vector<std::uint8_t> cells);

    /// Geodetic radians.
    LandClass classAt(double latitude, double longitude) const;
    LandClass cell(int row, int column) const;

    /// MODIS has one class for the sea and lakes. Water components that reach `elevation` deeper
    /// than `seaDepth` stay Ocean, the rest become InlandWater. Returns the cells reclassified.
    std::size_t separateInlandWater(const ElevationGrid& elevation, double seaDepth = -50.0);

    /// Per cell, not area-weighted.
    std::array<double, kLandClassCount> classFractions() const;

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint8_t>& cells() const { return cells_; }

    double cellLatitude(int row) const;
    double cellLongitude(int column) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> cells_;
};

} // namespace orbis
