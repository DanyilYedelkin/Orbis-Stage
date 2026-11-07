#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace orbis {

/// Geographic elevation grid with a low-passed mip pyramid.
///
///  - Global: longitude -180..180 (wrapping), latitude 90..-90 including the poles. File format
///    ".oeg": char[4] "OEG1" | uint32 width, height | float64 lonMin, lonMax, latMax, latMin |
///    int16[height][width] metres, little-endian (tools/prepare_etopo.py).
///  - Regional: a lat/lon rectangle clamped at its edges (.hgt tiles).
///
/// Voids are NaN and sampling near one returns NaN. Sampling is Catmull-Rom: bilinear is only
/// C0 and grid lines would show as lighting creases below the data resolution.
class ElevationGrid {
public:
    /// Throws std::runtime_error on any format problem.
    static ElevationGrid load(const std::filesystem::path& path);

    /// Global grid, row-major, north to south.
    ElevationGrid(int width, int height, std::vector<float> nodes);
    /// Stored as int16 at every level: half the memory, metre steps in the coarse levels.
    ElevationGrid(int width, int height, std::vector<std::int16_t> nodes);

    /// Bounds in radians, rows from latMax to latMin.
    static ElevationGrid regional(int width, int height, std::vector<float> nodes, double latMax,
                                  double latMin, double lonMin, double lonMax);

    /// Metres at geodetic radians, or NaN. `level` is the mip level (fractional levels blend).
    double sample(double latitude, double longitude, double level = 0.0) const;

    /// |level `base` - level `base + smoothing`| at the nodes of level `base`. Its level k
    /// matches level `base + k` here. Global grids only.
    ElevationGrid reliefMap(int smoothing, int base = 0) const;

    /// 1 where there is data, 0 at voids; at coarse levels a blurred coverage field.
    ElevationGrid validityMask() const;

    /// East-west node spacing at the equator for a sphere of `radius` metres.
    double nodeSpacing(int level, double radius) const;

    bool isGlobal() const { return global_; }
    bool contains(double latitude, double longitude) const;
    /// Approximate angular distance (radians) to the nearest edge.
    double angularDistanceToEdge(double latitude, double longitude) const;

    int levelCount() const { return static_cast<int>(levels_.size()); }
    int width() const { return global_ ? levels_.front().columns + 1 : levels_.front().columns; }
    int height() const { return levels_.front().rows; }

private:
    struct Level {
        int columns = 0; ///< Global grids store the wrapping meridian once.
        int rows = 0;
        bool global = true;
        std::vector<float> values;
        std::vector<std::int16_t> compact; ///< Used instead of `values` when not empty.

        double at(int row, int column) const;
        void set(int row, int column, double value);
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
