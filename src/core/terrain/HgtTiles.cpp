#include "core/terrain/HgtTiles.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace orbis {

std::optional<std::pair<int, int>> parseHgtName(std::string_view stem) {
    if (stem.size() != 7) {
        return std::nullopt;
    }
    const auto upper = [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); };
    const char ns = upper(stem[0]);
    const char ew = upper(stem[3]);
    if ((ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W')) {
        return std::nullopt;
    }
    for (std::size_t i : {1u, 2u, 4u, 5u, 6u}) {
        if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
            return std::nullopt;
        }
    }
    const int lat = (stem[1] - '0') * 10 + (stem[2] - '0');
    const int lon = (stem[4] - '0') * 100 + (stem[5] - '0') * 10 + (stem[6] - '0');
    if (lat > 90 || lon > 180) {
        return std::nullopt;
    }
    return std::pair{ns == 'N' ? lat : -lat, ew == 'E' ? lon : -lon};
}

namespace {

std::int16_t bigEndian(const unsigned char* bytes) {
    // Big-endian, regardless of the machine (SRTM convention).
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[0] << 8) | bytes[1]);
}

/// Reads the rows of a tile, calling store(row, column, value-or-NaN) for each node.
template <typename Store>
void readTile(const HgtTileFile& file, Store&& store, bool& hasVoids) {
    std::ifstream in(file.path, std::ios::binary);
    const auto n = static_cast<std::size_t>(file.samples);
    std::vector<unsigned char> row(n * 2);
    hasVoids = false;
    for (std::size_t r = 0; r < n; ++r) {
        in.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
        if (!in) {
            throw std::runtime_error("Truncated .hgt tile: " + file.path.string());
        }
        for (std::size_t c = 0; c < n; ++c) {
            const std::int16_t value = bigEndian(&row[2 * c]);
            if (value == -32768) {
                hasVoids = true;
                store(r, c, std::numeric_limits<float>::quiet_NaN());
            } else {
                store(r, c, static_cast<float>(value));
            }
        }
    }
}

constexpr double kDeg = std::numbers::pi / 180.0;

} // namespace

std::vector<HgtTileFile> scanHgtDirectory(const std::filesystem::path& directory) {
    std::vector<HgtTileFile> tiles;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return tiles;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        std::string ext = entry.path().extension().string();
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!entry.is_regular_file() || ext != ".hgt") {
            continue;
        }
        const auto corner = parseHgtName(entry.path().stem().string());
        if (!corner) {
            continue;
        }
        // Resolution from the file size (N * N * 2 bytes).
        const auto bytes = std::filesystem::file_size(entry.path());
        const auto side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(bytes) / 2.0)));
        if (side < 3 || static_cast<std::uintmax_t>(side) * static_cast<std::uintmax_t>(side) * 2 != bytes) {
            throw std::runtime_error("Not a square .hgt tile: " + entry.path().string());
        }
        tiles.push_back({entry.path(), corner->first, corner->second, side});
    }
    return tiles;
}

HgtTile loadHgtTile(const HgtTileFile& file) {
    const auto n = static_cast<std::size_t>(file.samples);
    std::vector<float> nodes(n * n);
    bool hasVoids = false;
    readTile(file, [&](std::size_t r, std::size_t c, float v) { nodes[r * n + c] = v; }, hasVoids);
    // Row 0 is the tile's NORTH edge (lat + 1).
    return {ElevationGrid::regional(file.samples, file.samples, std::move(nodes), (file.lat + 1) * kDeg,
                                    file.lat * kDeg, file.lon * kDeg, (file.lon + 1) * kDeg),
            hasVoids};
}

std::optional<HgtMosaic> loadHgtMosaic(const std::filesystem::path& directory) {
    const std::vector<HgtTileFile> tiles = scanHgtDirectory(directory);
    if (tiles.empty()) {
        return std::nullopt;
    }

    // All tiles must have the same resolution to form one grid.
    int n = 0;
    HgtMosaic mosaic{ElevationGrid(3, 3, std::vector<float>(9, 0.0f)), 0, 0, 90, -90, 180, -180};
    for (const HgtTileFile& t : tiles) {
        if (n != 0 && t.samples != n) {
            throw std::runtime_error("Mixed .hgt resolutions in " + directory.string() +
                                     " (use only 3\" or only 1\" tiles)");
        }
        n = t.samples;
        mosaic.latMin = std::min(mosaic.latMin, t.lat);
        mosaic.latMax = std::max(mosaic.latMax, t.lat + 1);
        mosaic.lonMin = std::min(mosaic.lonMin, t.lon);
        mosaic.lonMax = std::max(mosaic.lonMax, t.lon + 1);
    }

    const int perDegree = n - 1;
    const auto width = static_cast<std::size_t>((mosaic.lonMax - mosaic.lonMin) * perDegree + 1);
    const auto height = static_cast<std::size_t>((mosaic.latMax - mosaic.latMin) * perDegree + 1);
    if (width * height > 400'000'000u) {
        throw std::runtime_error("The .hgt tiles span too large an area for one mosaic; "
                                 "keep only the region you want to fly over");
    }
    // Gaps inside the bounding box (tiles you did not download) stay NaN = "no data here".
    std::vector<float> nodes(width * height, std::numeric_limits<float>::quiet_NaN());
    for (const HgtTileFile& t : tiles) {
        // Tile row 0 is its NORTH edge (lat + 1); the mosaic's row 0 is latMax.
        const std::size_t row0 = static_cast<std::size_t>((mosaic.latMax - (t.lat + 1)) * perDegree);
        const std::size_t col0 = static_cast<std::size_t>((t.lon - mosaic.lonMin) * perDegree);
        bool voids = false;
        readTile(t, [&](std::size_t r, std::size_t c, float v) { nodes[(row0 + r) * width + col0 + c] = v; },
                 voids);
    }

    mosaic.grid = ElevationGrid::regional(static_cast<int>(width), static_cast<int>(height),
                                          std::move(nodes), mosaic.latMax * kDeg,
                                          mosaic.latMin * kDeg, mosaic.lonMin * kDeg,
                                          mosaic.lonMax * kDeg);
    mosaic.tileCount = static_cast<int>(tiles.size());
    mosaic.samplesPerTile = n;
    return mosaic;
}

} // namespace orbis
