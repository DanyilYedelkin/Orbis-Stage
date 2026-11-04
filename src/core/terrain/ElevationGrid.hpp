#pragma once

#include <filesystem>
#include <vector>

namespace orbis {

/// Elevation grid in geographic coordinates (a "DEM"), with a mip pyramid.
///
/// Two topologies:
///  - GLOBAL: longitude -180 .. +180 (first and last column are the same meridian and wrap),
///    latitude +90 .. -90 including both poles. This is the ETOPO base layer. File format
///    ".oeg": char[4] "OEG1" | uint32 width, height | float64 lonMin, lonMax, latMax, latMin |
///    int16[height][width] metres, little-endian (written by tools/prepare_etopo.py).
///  - REGIONAL: any lat/lon rectangle, clamped at its edges. Used for high-resolution tiles.
///
/// Missing data ("voids") are NaN. Sampling a void, or near one, returns NaN, so the caller can
/// fall back to another source instead of using made-up numbers.
///
/// Sampling is bicubic (Catmull-Rom): bilinear interpolation is only C0, so every grid line
/// would show as a crease in the lighting once you zoom in below the data resolution. Bicubic is
/// C1 - slopes are continuous - and the procedural detail on top hides the rest.
class ElevationGrid {
public:
    /// Throws std::runtime_error with a readable message on any format problem.
    static ElevationGrid load(const std::filesystem::path& path);

    /// Global grid from node values (row-major, north to south). Mainly for tests.
    ElevationGrid(int width, int height, std::vector<float> nodes);

    /// Regional grid; bounds in RADIANS, nodes row-major from latMax (north) to latMin.
    static ElevationGrid regional(int width, int height, std::vector<float> nodes, double latMax,
                                  double latMin, double lonMin, double lonMax);

    /// Elevation in metres at geodetic latitude / longitude (radians), or NaN on missing data.
    /// `level` selects pre-filtered detail: 0 = full resolution, 1 = half, ... Fractional
    /// levels blend two neighbours (like trilinear texture filtering).
    double sample(double latitude, double longitude, double level = 0.0) const;

    /// A new grid holding the LOCAL RELIEF of this one: |full resolution - mip level `smoothing`|
    /// at every node. Sample it at level >= `smoothing` to get a smooth "how mountainous is it
    /// here" field (plains: tens of metres, Alps: ~1.5 km). Global grids only.
    ElevationGrid reliefMap(int smoothing) const;

    /// A grid of the same shape holding 1 where this one has data and 0 at voids. Sampled at a
    /// coarse level it becomes a smooth "coverage" field (the mip filter blurs it), used to blend
    /// gracefully into other data near ANY boundary: outer edge, missing tile, or void.
    ElevationGrid validityMask() const;

    /// Distance between nodes along the equator (east-west) at `level`, for `radius` metres.
    double nodeSpacing(int level, double radius) const;

    bool isGlobal() const { return global_; }
    bool contains(double latitude, double longitude) const;
    /// Distance (radians of arc, roughly) from a contained point to the nearest grid edge.
    double angularDistanceToEdge(double latitude, double longitude) const;

    int levelCount() const { return static_cast<int>(levels_.size()); }
    int width() const { return global_ ? levels_.front().columns + 1 : levels_.front().columns; }
    int height() const { return levels_.front().rows; }

private:
    struct Level {
        int columns = 0; ///< Global: unique meridians (wrapping). Regional: all columns.
        int rows = 0;
        bool global = true;
        std::vector<float> values;

        /// Stored as float (half the memory), read as double: all maths happens in double.
        double at(int row, int column) const;
    };

    ElevationGrid() = default;
    void buildPyramid();
    static double bicubic(const Level& level, double row, double column);

    std::vector<Level> levels_;
    bool global_ = true;
    double latMax_ = 0.0;
    double latMin_ = 0.0;
    double lonMin_ = 0.0;
    double lonMax_ = 0.0;
};

} // namespace orbis
