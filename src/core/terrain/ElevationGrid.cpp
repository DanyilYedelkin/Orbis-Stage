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
#include <thread>

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

/// Rows are independent; the 1' grid's first pyramid level alone has 58 M nodes.
template <typename F>
void forEachRow(int rows, const F& f) {
    const int threads = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 16u));
    if (rows < 64 || threads == 1) {
        for (int r = 0; r < rows; ++r) {
            f(r);
        }
        return;
    }
    std::vector<std::jthread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&f, rows, threads, t] {
            for (int r = rows * t / threads; r < rows * (t + 1) / threads; ++r) {
                f(r);
            }
        });
    }
}

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
    const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) +
                          static_cast<std::size_t>(column);
    return compact.empty() ? static_cast<double>(values[i]) : static_cast<double>(compact[i]);
}

void ElevationGrid::Level::set(int row, int column, double value) {
    const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) +
                          static_cast<std::size_t>(column);
    if (compact.empty()) {
        values[i] = static_cast<float>(value);
    } else {
        compact[i] = static_cast<std::int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
    }
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

ElevationGrid::ElevationGrid(int width, int height, std::vector<std::int16_t> nodes) {
    if (width < 3 || height < 3 || nodes.size() != static_cast<std::size_t>(width) *
                                                       static_cast<std::size_t>(height)) {
        throw std::invalid_argument("ElevationGrid: inconsistent size");
    }
    // Drop the duplicate +180 column in place; every row only moves towards the front.
    const auto columns = static_cast<std::size_t>(width - 1);
    for (std::size_t r = 1; r < static_cast<std::size_t>(height); ++r) {
        std::memmove(nodes.data() + r * columns, nodes.data() + r * (columns + 1),
                     columns * sizeof(std::int16_t));
    }
    nodes.resize(columns * static_cast<std::size_t>(height));
    Level base;
    base.columns = width - 1;
    base.rows = height;
    base.compact = std::move(nodes);
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

    std::vector<std::int16_t> nodes(static_cast<std::size_t>(width) * height);
    in.read(reinterpret_cast<char*>(nodes.data()),
            static_cast<std::streamsize>(nodes.size() * sizeof(std::int16_t)));
    if (!in) {
        throw std::runtime_error("Elevation grid is truncated: " + path.string());
    }
    if constexpr (std::endian::native == std::endian::big) {
        for (std::int16_t& v : nodes) {
            const auto u = static_cast<std::uint16_t>(v);
            v = static_cast<std::int16_t>(static_cast<std::uint16_t>((u >> 8) | (u << 8)));
        }
    }
    return ElevationGrid(static_cast<int>(width), static_cast<int>(height), std::move(nodes));
}

void ElevationGrid::buildPyramid() {
    // Each level halves the node count after a [1 2 1] / 4 low-pass in both directions; without
    // the filter coarse levels alias (a 2 km ridge could survive as a 150 km bump). Voids (NaN)
    // propagate. A wrapping axis of n nodes becomes ceil(n / 2), a clamped one of n intervals
    // ceil(n / 2) intervals. When n is odd the coarse nodes fall between fine ones and the
    // filtered field is interpolated linearly.
    const auto filtered = [](const Level& fine, int row, int column) {
        constexpr std::array<double, 3> w = {0.25, 0.5, 0.25};
        double sum = 0.0;
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                sum += w[static_cast<std::size_t>(dr + 1)] * w[static_cast<std::size_t>(dc + 1)] *
                       fine.at(row + dr, column + dc);
            }
        }
        return sum;
    };

    while (levels_.size() < 12) {
        const Level& fine = levels_.back();
        if (fine.rows < 5 || fine.columns < 5) {
            break;
        }
        Level coarse;
        coarse.global = fine.global;
        coarse.columns = fine.global ? (fine.columns + 1) / 2 : fine.columns / 2 + 1;
        coarse.rows = fine.rows / 2 + 1;
        const auto count =
            static_cast<std::size_t>(coarse.columns) * static_cast<std::size_t>(coarse.rows);
        if (fine.compact.empty()) {
            coarse.values.resize(count);
        } else {
            coarse.compact.resize(count);
        }

        const double rowStep = static_cast<double>(fine.rows - 1) / (coarse.rows - 1);
        const double columnStep =
            fine.global ? static_cast<double>(fine.columns) / coarse.columns
                        : static_cast<double>(fine.columns - 1) / (coarse.columns - 1);
        const bool exact = rowStep == 2.0 && columnStep == 2.0;
        forEachRow(coarse.rows, [&](int r) {
            const double fr = r * rowStep;
            const int r0 = static_cast<int>(fr);
            const double tr = fr - r0;
            for (int c = 0; c < coarse.columns; ++c) {
                if (exact) {
                    coarse.set(r, c, filtered(fine, 2 * r, 2 * c));
                    continue;
                }
                const double fc = c * columnStep;
                const int c0 = static_cast<int>(fc);
                const double tc = fc - c0;
                // Zero weights are skipped so a void next to a node does not leak into it.
                double sum = 0.0;
                for (int k = 0; k < 4; ++k) {
                    const double w = (k & 1 ? tc : 1.0 - tc) * (k & 2 ? tr : 1.0 - tr);
                    if (w > 0.0) {
                        sum += w * filtered(fine, r0 + (k >> 1), c0 + (k & 1));
                    }
                }
                coarse.set(r, c, sum);
            }
        });
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

ElevationGrid ElevationGrid::reliefMap(int smoothing, int base) const {
    if (!global_) {
        throw std::logic_error("ElevationGrid::reliefMap: global grids only");
    }
    base = std::clamp(base, 0, levelCount() - 1);
    const Level& fine = levels_[static_cast<std::size_t>(base)];
    const int width = fine.columns + 1; // including the duplicated +180 column
    std::vector<float> nodes(static_cast<std::size_t>(width) * static_cast<std::size_t>(fine.rows));
    forEachRow(fine.rows, [&](int r) {
        const double latitude = kPi / 2.0 - kPi * r / (fine.rows - 1);
        for (int c = 0; c < width; ++c) {
            const double longitude = -kPi + 2.0 * kPi * c / fine.columns;
            const double smooth = sample(latitude, longitude, base + smoothing);
            const std::size_t i = static_cast<std::size_t>(r) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(c);
            nodes[i] = static_cast<float>(std::abs(fine.at(r, c) - smooth));
        }
    });
    return ElevationGrid(width, fine.rows, std::move(nodes));
}

ElevationGrid ElevationGrid::validityMask() const {
    ElevationGrid mask;
    mask.global_ = global_;
    mask.latMax_ = latMax_;
    mask.latMin_ = latMin_;
    mask.lonMin_ = lonMin_;
    mask.lonMax_ = lonMax_;
    const Level& base = levels_.front();
    Level valid{base.columns, base.rows, base.global, {}, {}};
    valid.values.resize(static_cast<std::size_t>(base.columns) *
                        static_cast<std::size_t>(base.rows));
    for (int r = 0; r < base.rows; ++r) {
        for (int c = 0; c < base.columns; ++c) {
            valid.set(r, c, std::isnan(base.at(r, c)) ? 0.0 : 1.0);
        }
    }
    mask.levels_.push_back(std::move(valid));
    mask.buildPyramid();
    return mask;
}

double ElevationGrid::nodeSpacing(int level, double radius) const {
    const Level& lv = levels_[static_cast<std::size_t>(std::clamp(level, 0, levelCount() - 1))];
    return global_ ? 2.0 * kPi * radius / lv.columns
                   : (lonMax_ - lonMin_) * radius / (lv.columns - 1);
}

} // namespace orbis
