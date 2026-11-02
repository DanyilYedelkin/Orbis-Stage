#pragma once

#include <filesystem>
#include <vector>

namespace orbis {

/// Global elevation grid in geographic coordinates (a "DEM"), with a mip pyramid.
///
/// Layout: grid-registered nodes, longitude -180 .. +180 (first and last column are the same
/// meridian), latitude +90 .. -90 (row 0 = North Pole). File format ".oeg":
///     char[4] "OEG1" | uint32 width, height | float64 lonMin, lonMax, latMax, latMin |
///     int16[height][width] metres, little-endian
/// produced by tools/prepare_etopo.py.
///
/// Sampling is bicubic (Catmull-Rom): bilinear interpolation is only C0, so every grid line
/// would show as a crease in the lighting once you zoom in below the data resolution. Bicubic is
/// C1 - slopes are continuous - and the procedural detail on top hides the rest.
class ElevationGrid {
public:
    /// Throws std::runtime_error with a readable message on any format problem.
    static ElevationGrid load(const std::filesystem::path& path);

    /// From node values (row-major, north to south). Mainly for tests.
    ElevationGrid(int width, int height, std::vector<float> nodes);

    /// Elevation in metres at geodetic latitude / longitude (radians).
    /// `level` selects pre-filtered detail: 0 = full resolution, 1 = half, ... Fractional
    /// levels blend two neighbours (like trilinear texture filtering).
    double sample(double latitude, double longitude, double level = 0.0) const;

    /// A new grid holding the LOCAL RELIEF of this one: |full resolution - mip level `smoothing`|
    /// at every node. Sample it at level >= `smoothing` to get a smooth "how mountainous is it
    /// here" field (plains: tens of metres, Alps: ~1.5 km).
    ElevationGrid reliefMap(int smoothing) const;

    /// Distance between nodes along the equator at `level`, for a planet of `radius` metres.
    double nodeSpacing(int level, double radius) const;

    int levelCount() const { return static_cast<int>(levels_.size()); }
    int width() const { return levels_.front().columns + 1; }
    int height() const { return levels_.front().rows; }

private:
    /// One mip level. Stores `columns` unique meridians (the duplicated +180 column is dropped,
    /// longitude wraps around instead) and `rows` parallels including both poles.
    struct Level {
        int columns = 0;
        int rows = 0;
        std::vector<float> values;

        /// Stored as float (half the memory), read as double: all maths happens in double.
        double at(int row, int column) const;
    };

    ElevationGrid() = default;
    void buildPyramid();
    static double bicubic(const Level& level, double row, double column);

    std::vector<Level> levels_;
};

} // namespace orbis
