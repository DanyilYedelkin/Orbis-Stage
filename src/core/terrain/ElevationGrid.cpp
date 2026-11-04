#include "core/terrain/ElevationGrid.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace orbis {

namespace {

template <typename T>
T readLittleEndian(std::ifstream& in) {
    std::array<unsigned char, sizeof(T)> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()), sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(bytes.begin(), bytes.end());
    }
    T value;
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

/// Catmull-Rom weights for the 4 nodes around a sample at fraction t in [0, 1).
/// Reproduces linear functions exactly and has continuous first derivatives.
std::array<double, 4> catmullRom(double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return {-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t,
            0.5 * t3 - 0.5 * t2};
}

constexpr double kPi = std::numbers::pi;

} // namespace

double ElevationGrid::Level::at(int row, int column) const {
    if (global) {
        // Rows beyond a pole continue on the opposite meridian: one row "north" of the North
        // Pole at longitude L is the first row south of it at L + 180. That keeps bicubic
        // sampling correct and seamless all the way to the poles.
        if (row < 0) {
            row = -row;
            column += columns / 2;
        } else if (row > rows - 1) {
            row = 2 * (rows - 1) - row;
            column += columns / 2;
        }
        column %= columns; // longitude wraps (the date line is not an edge)
        if (column < 0) {
            column += columns;
        }
    } else {
        row = std::clamp(row, 0, rows - 1); // regional grids simply stop at their edges
        column = std::clamp(column, 0, columns - 1);
    }
    return static_cast<double>(values[static_cast<std::size_t>(row) *
                                          static_cast<std::size_t>(columns) +
                                      static_cast<std::size_t>(column)]);
}

ElevationGrid::ElevationGrid(int width, int height, std::vector<float> nodes) {
    if (width < 3 || height < 3 || nodes.size() != static_cast<std::size_t>(width) *
                                                       static_cast<std::size_t>(height)) {
        throw std::invalid_argument("ElevationGrid: inconsistent size");
    }
    Level base;
    base.columns = width - 1;
    base.rows = height;
    base.values.reserve(static_cast<std::size_t>(base.columns) * static_cast<std::size_t>(height));
    for (int r = 0; r < height; ++r) {
        const auto row = nodes.begin() + static_cast<std::ptrdiff_t>(r) * width;
        base.values.insert(base.values.end(), row, row + base.columns); // drop duplicate column
    }
    levels_.push_back(std::move(base));
    latMax_ = kPi / 2.0;
    latMin_ = -kPi / 2.0;
    lonMin_ = -kPi;
    lonMax_ = kPi;
    buildPyramid();
}

ElevationGrid ElevationGrid::regional(int width, int height, std::vector<float> nodes,
                                      double latMax, double latMin, double lonMin, double lonMax) {
    if (width < 2 || height < 2 ||
        nodes.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
        !(latMax > latMin) || !(lonMax > lonMin)) {
        throw std::invalid_argument("ElevationGrid::regional: inconsistent size or bounds");
    }
    ElevationGrid grid;
    grid.global_ = false;
    grid.latMax_ = latMax;
    grid.latMin_ = latMin;
    grid.lonMin_ = lonMin;
    grid.lonMax_ = lonMax;
    Level base;
    base.columns = width;
    base.rows = height;
    base.global = false;
    base.values = std::move(nodes);
    grid.levels_.push_back(std::move(base));
    grid.buildPyramid();
    return grid;
}

bool ElevationGrid::contains(double latitude, double longitude) const {
    return global_ || (latitude <= latMax_ && latitude >= latMin_ && longitude >= lonMin_ &&
                       longitude <= lonMax_);
}

double ElevationGrid::angularDistanceToEdge(double latitude, double longitude) const {
    if (global_) {
        return kPi;
    }
    const double dLat = std::min(latMax_ - latitude, latitude - latMin_);
    const double dLon = std::min(longitude - lonMin_, lonMax_ - longitude) * std::cos(latitude);
    return std::min(dLat, dLon);
}

ElevationGrid ElevationGrid::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open elevation data: " + path.string());
    }
    char magic[4] = {};
    in.read(magic, 4);
    if (!in || std::memcmp(magic, "OEG1", 4) != 0) {
        throw std::runtime_error("Not an Orbis elevation grid (bad magic): " + path.string());
    }
    const auto width = readLittleEndian<std::uint32_t>(in);
    const auto height = readLittleEndian<std::uint32_t>(in);
    const double lonMin = readLittleEndian<double>(in);
    const double lonMax = readLittleEndian<double>(in);
    const double latMax = readLittleEndian<double>(in);
    const double latMin = readLittleEndian<double>(in);
    if (!in || width < 3 || height < 3 || width > 200000 || height > 100000) {
        throw std::runtime_error("Elevation grid header is corrupt: " + path.string());
    }
    if (lonMin != -180.0 || lonMax != 180.0 || latMax != 90.0 || latMin != -90.0) {
        throw std::runtime_error("Only global grids (-180..180, 90..-90) are supported: " +
                                 path.string());
    }

    std::vector<float> nodes(static_cast<std::size_t>(width) * height);
    for (float& v : nodes) {
        v = static_cast<float>(readLittleEndian<std::int16_t>(in));
    }
    if (!in) {
        throw std::runtime_error("Elevation grid is truncated: " + path.string());
    }
    return ElevationGrid(static_cast<int>(width), static_cast<int>(height), std::move(nodes));
}

void ElevationGrid::buildPyramid() {
    // Each level keeps every second node, after a [1 2 1] / 4 low-pass in both directions.
    // Without the filter the coarse levels would alias (a 2 km wide ridge could survive as a
    // 150 km wide bump). Voids (NaN) propagate, so coarse levels never invent data either.
    // A wrapping axis of n nodes becomes n / 2; a clamped axis keeps both ends: (n - 1) / 2 + 1.
    while (levels_.size() < 12) {
        const Level& fine = levels_.back();
        const bool columnsOk = fine.global ? fine.columns % 2 == 0 : (fine.columns - 1) % 2 == 0;
        if (!columnsOk || (fine.rows - 1) % 2 != 0 || fine.rows < 5 || fine.columns < 5) {
            break;
        }
        Level coarse;
        coarse.global = fine.global;
        coarse.columns = fine.global ? fine.columns / 2 : (fine.columns - 1) / 2 + 1;
        coarse.rows = (fine.rows - 1) / 2 + 1;
        coarse.values.resize(static_cast<std::size_t>(coarse.columns) *
                             static_cast<std::size_t>(coarse.rows));
        constexpr std::array<double, 3> w = {0.25, 0.5, 0.25};
        for (int r = 0; r < coarse.rows; ++r) {
            for (int c = 0; c < coarse.columns; ++c) {
                double sum = 0.0;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        sum += w[static_cast<std::size_t>(dr + 1)] *
                               w[static_cast<std::size_t>(dc + 1)] *
                               fine.at(2 * r + dr, 2 * c + dc);
                    }
                }
                coarse.values[static_cast<std::size_t>(r) * static_cast<std::size_t>(coarse.columns) +
                              static_cast<std::size_t>(c)] = static_cast<float>(sum);
            }
        }
        levels_.push_back(std::move(coarse));
    }
}

double ElevationGrid::bicubic(const Level& level, double row, double column) {
    const double r0 = std::floor(row);
    const double c0 = std::floor(column);
    const std::array<double, 4> wr = catmullRom(row - r0);
    const std::array<double, 4> wc = catmullRom(column - c0);
    const int ri = static_cast<int>(r0);
    const int ci = static_cast<int>(c0);

    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        double rowSum = 0.0;
        for (int j = 0; j < 4; ++j) {
            rowSum += wc[static_cast<std::size_t>(j)] * level.at(ri - 1 + i, ci - 1 + j);
        }
        sum += wr[static_cast<std::size_t>(i)] * rowSum;
    }
    return sum;
}

double ElevationGrid::sample(double latitude, double longitude, double level) const {
    const double clamped = std::clamp(level, 0.0, static_cast<double>(levels_.size() - 1));
    const auto l0 = static_cast<std::size_t>(clamped);
    const double t = clamped - static_cast<double>(l0);

    const auto sampleLevel = [&](const Level& lv) {
        // Fractional node coordinates: row 0 = latMax, column 0 = lonMin.
        const double row = (latMax_ - latitude) / (latMax_ - latMin_) * (lv.rows - 1);
        const double column = lv.global
                                  ? (longitude - lonMin_) / (2.0 * kPi) * lv.columns
                                  : (longitude - lonMin_) / (lonMax_ - lonMin_) * (lv.columns - 1);
        return bicubic(lv, row, column);
    };

    const double a = sampleLevel(levels_[l0]);
    if (t <= 0.0 || l0 + 1 >= levels_.size()) {
        return a;
    }
    return a + t * (sampleLevel(levels_[l0 + 1]) - a);
}

ElevationGrid ElevationGrid::reliefMap(int smoothing) const {
    if (!global_) {
        throw std::logic_error("ElevationGrid::reliefMap: global grids only");
    }
    const Level& fine = levels_.front();
    std::vector<float> nodes;
    nodes.reserve(static_cast<std::size_t>(fine.columns + 1) * static_cast<std::size_t>(fine.rows));
    for (int r = 0; r < fine.rows; ++r) {
        const double latitude = kPi / 2.0 - kPi * r / (fine.rows - 1);
        for (int c = 0; c <= fine.columns; ++c) { // include the duplicated +180 column
            const double longitude = -kPi + 2.0 * kPi * c / fine.columns;
            const double smooth = sample(latitude, longitude, static_cast<double>(smoothing));
            nodes.push_back(static_cast<float>(std::abs(fine.at(r, c) - smooth)));
        }
    }
    return ElevationGrid(fine.columns + 1, fine.rows, std::move(nodes));
}

ElevationGrid ElevationGrid::validityMask() const {
    ElevationGrid mask = *this;
    mask.levels_.resize(1);
    for (float& v : mask.levels_.front().values) {
        v = std::isnan(v) ? 0.0f : 1.0f;
    }
    mask.buildPyramid();
    return mask;
}

double ElevationGrid::nodeSpacing(int level, double radius) const {
    const Level& lv = levels_[static_cast<std::size_t>(std::clamp(level, 0, levelCount() - 1))];
    return global_ ? 2.0 * kPi * radius / lv.columns
                   : (lonMax_ - lonMin_) * radius / (lv.columns - 1);
}

} // namespace orbis
