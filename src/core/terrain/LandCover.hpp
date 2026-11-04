#pragma once

#include "core/terrain/ElevationGrid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace orbis {

/// IGBP land cover classes as used by MODIS MCD12C1 (values 0..16), plus one class Orbis derives
/// itself: MODIS has a single "water" class for the open ocean and for lakes, and the renderer
/// needs to tell them apart (see LandCover::separateInlandWater).
enum class LandClass : std::uint8_t {
    Ocean = 0, ///< MODIS "water"; after separateInlandWater() only water connected to the sea.
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
    InlandWater = 17, ///< Derived: lakes and reservoirs.
};

inline constexpr int kLandClassCount = 18;
inline constexpr int kModisClassCount = 17; ///< Classes stored in the .olc file.

std::string_view landClassName(LandClass c);

/// Global land cover map: one class per cell of a regular latitude/longitude grid.
///
/// The grid is CELL-registered (each value covers an area; cell (0, 0) is the north-west corner
/// at 90 N, 180 W), unlike the node-registered elevation grids. File format ".olc": char[4]
/// "OLC1" | uint32 width, height, classCount | run-length rows (uint8 class, LEB128 length) |
/// uint32 FNV-1a of the decoded cells. Written by tools/prepare_landcover.py.
///
/// Classes are categorical: they are looked up (nearest cell), never interpolated - the average
/// of "forest" and "desert" is not a class. Smooth transitions are the renderer's job.
class LandCover {
public:
    /// Throws std::runtime_error with a readable message on any format problem.
    static LandCover load(const std::filesystem::path& path);

    /// From raw cells, row-major, north to south. Mainly for tests.
    LandCover(int width, int height, std::vector<std::uint8_t> cells);

    /// Class of the cell containing geodetic latitude / longitude (radians).
    LandClass classAt(double latitude, double longitude) const;
    LandClass cell(int row, int column) const;

    /// MODIS stores the sea and lakes as the same "water" class. Water cells connected (through
    /// other water cells, longitude wrapping) to a cell where `elevation` is deeper than
    /// `seaDepth` metres stay Ocean; all other water becomes InlandWater. The renderer draws the
    /// ocean from the elevation (below sea level) and needs the class only for lakes, so a
    /// coastal cell half on land must not paint its land half as water.
    /// Returns the number of cells reclassified.
    std::size_t separateInlandWater(const ElevationGrid& elevation, double seaDepth = -50.0);

    /// Fraction of the cells of each class over the whole map (not area-weighted).
    std::array<double, kLandClassCount> classFractions() const;

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint8_t>& cells() const { return cells_; }

    /// Latitude / longitude (radians) of the centre of a cell.
    double cellLatitude(int row) const;
    double cellLongitude(int column) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> cells_;
};

} // namespace orbis
