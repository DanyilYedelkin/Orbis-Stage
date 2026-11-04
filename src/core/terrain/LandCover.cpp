#include "core/terrain/LandCover.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <string>

namespace orbis {

namespace {

constexpr double kPi = std::numbers::pi;

std::uint32_t fnv1a(const std::vector<std::uint8_t>& data) {
    std::uint32_t h = 0x811C9DC5u;
    for (const std::uint8_t b : data) {
        h = (h ^ b) * 0x01000193u;
    }
    return h;
}

template <typename T>
T readLittleEndian(const std::vector<char>& bytes, std::size_t& pos) {
    if (pos + sizeof(T) > bytes.size()) {
        throw std::runtime_error("truncated");
    }
    std::array<char, sizeof(T)> raw{};
    std::memcpy(raw.data(), bytes.data() + pos, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(raw.begin(), raw.end());
    }
    pos += sizeof(T);
    T value;
    std::memcpy(&value, raw.data(), sizeof(T));
    return value;
}

} // namespace

std::string_view landClassName(LandClass c) {
    constexpr std::array<std::string_view, kLandClassCount> names = {
        "ocean",          "evergreen needleleaf forest", "evergreen broadleaf forest",
        "deciduous needleleaf forest", "deciduous broadleaf forest", "mixed forest",
        "closed shrubland", "open shrubland", "woody savanna", "savanna", "grassland",
        "permanent wetland", "cropland", "urban", "cropland / natural mosaic", "snow and ice",
        "barren", "inland water"};
    const auto i = static_cast<std::size_t>(c);
    return i < names.size() ? names[i] : "unknown";
}

LandCover::LandCover(int width, int height, std::vector<std::uint8_t> cells)
    : width_(width), height_(height), cells_(std::move(cells)) {
    if (width < 2 || height < 2 ||
        cells_.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        throw std::invalid_argument("LandCover: inconsistent size");
    }
    for (const std::uint8_t c : cells_) {
        if (c >= kLandClassCount) {
            throw std::invalid_argument("LandCover: class value out of range");
        }
    }
}

LandCover LandCover::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Cannot open land cover data: " + path.string());
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>()};
    const auto fail = [&](const std::string& what) {
        return std::runtime_error("Land cover file " + what + ": " + path.string());
    };
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "OLC1", 4) != 0) {
        throw fail("has a bad magic (not an Orbis land cover map)");
    }
    try {
        std::size_t pos = 4;
        const auto width = readLittleEndian<std::uint32_t>(bytes, pos);
        const auto height = readLittleEndian<std::uint32_t>(bytes, pos);
        const auto classCount = readLittleEndian<std::uint32_t>(bytes, pos);
        if (width < 2 || height < 2 || width > 100000 || height > 50000 ||
            classCount != static_cast<std::uint32_t>(kModisClassCount)) {
            throw fail("has a corrupt header");
        }
        std::vector<std::uint8_t> cells;
        cells.reserve(static_cast<std::size_t>(width) * height);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::uint32_t filled = 0;
            while (filled < width) {
                if (pos >= bytes.size()) {
                    throw fail("is truncated");
                }
                const auto value = static_cast<std::uint8_t>(bytes[pos++]);
                std::uint64_t length = 0;
                for (int shift = 0;; shift += 7) {
                    if (pos >= bytes.size() || shift > 28) {
                        throw fail("has a corrupt run length");
                    }
                    const auto b = static_cast<std::uint8_t>(bytes[pos++]);
                    length |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
                    if ((b & 0x80u) == 0) {
                        break;
                    }
                }
                if (value >= classCount || length == 0 || filled + length > width) {
                    throw fail("has an invalid run");
                }
                cells.insert(cells.end(), static_cast<std::size_t>(length), value);
                filled += static_cast<std::uint32_t>(length);
            }
        }
        const auto expected = readLittleEndian<std::uint32_t>(bytes, pos);
        if (fnv1a(cells) != expected) {
            throw fail("fails its checksum");
        }
        return LandCover(static_cast<int>(width), static_cast<int>(height), std::move(cells));
    } catch (const std::runtime_error& e) {
        if (std::string_view(e.what()) == "truncated") {
            throw fail("is truncated");
        }
        throw;
    }
}

LandClass LandCover::cell(int row, int column) const {
    row = std::clamp(row, 0, height_ - 1);
    column %= width_;
    if (column < 0) {
        column += width_;
    }
    return static_cast<LandClass>(
        cells_[static_cast<std::size_t>(row) * static_cast<std::size_t>(width_) +
               static_cast<std::size_t>(column)]);
}

LandClass LandCover::classAt(double latitude, double longitude) const {
    const auto row = static_cast<int>(std::floor((kPi / 2.0 - latitude) / kPi * height_));
    const auto column = static_cast<int>(std::floor((longitude + kPi) / (2.0 * kPi) * width_));
    return cell(row, column);
}

double LandCover::cellLatitude(int row) const { return kPi / 2.0 - kPi * (row + 0.5) / height_; }

double LandCover::cellLongitude(int column) const {
    return -kPi + 2.0 * kPi * (column + 0.5) / width_;
}

std::size_t LandCover::separateInlandWater(const ElevationGrid& elevation, double seaDepth) {
    // Connected components of water cells (4-neighbourhood, longitude wraps, the poles do not).
    // A component is the sea as soon as ONE of its cells is deep; sampling stops there, so the
    // whole ocean costs a handful of samples and only lakes are sampled cell by cell.
    const std::size_t n = cells_.size();
    const auto w = static_cast<std::size_t>(width_);
    std::vector<std::uint8_t> visited(n, 0);
    std::vector<std::uint32_t> component;
    std::vector<std::uint32_t> stack;
    std::size_t reclassified = 0;
    const auto water = [this](std::size_t i) {
        return cells_[i] == static_cast<std::uint8_t>(LandClass::Ocean);
    };

    for (std::size_t start = 0; start < n; ++start) {
        if (visited[start] || !water(start)) {
            continue;
        }
        component.clear();
        stack.assign(1, static_cast<std::uint32_t>(start));
        visited[start] = 1;
        bool sea = false;
        while (!stack.empty()) {
            const std::uint32_t i = stack.back();
            stack.pop_back();
            component.push_back(i);
            const auto row = static_cast<int>(i / w);
            const auto col = static_cast<int>(i % w);
            if (!sea && elevation.sample(cellLatitude(row), cellLongitude(col)) < seaDepth) {
                sea = true;
            }
            const auto visit = [&](int r, int c) {
                if (r < 0 || r >= height_) {
                    return;
                }
                c = (c + width_) % width_;
                const std::size_t j = static_cast<std::size_t>(r) * w + static_cast<std::size_t>(c);
                if (!visited[j] && water(j)) {
                    visited[j] = 1;
                    stack.push_back(static_cast<std::uint32_t>(j));
                }
            };
            visit(row - 1, col);
            visit(row + 1, col);
            visit(row, col - 1);
            visit(row, col + 1);
        }
        if (!sea) {
            for (const std::uint32_t i : component) {
                cells_[i] = static_cast<std::uint8_t>(LandClass::InlandWater);
            }
            reclassified += component.size();
        }
    }
    return reclassified;
}

std::array<double, kLandClassCount> LandCover::classFractions() const {
    std::array<double, kLandClassCount> fractions{};
    for (const std::uint8_t c : cells_) {
        fractions[c] += 1.0;
    }
    for (double& f : fractions) {
        f /= static_cast<double>(cells_.size());
    }
    return fractions;
}

} // namespace orbis
