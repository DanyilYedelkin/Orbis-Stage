#pragma once

#include "core/terrain/ElevationGrid.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orbis {

/// SRTM .hgt: one degree square named after its SW corner, N x N big-endian int16, rows north to
/// south, edges shared with neighbours. N = 1201 or 3601; -32768 is a void.
struct HgtMosaic {
    ElevationGrid grid; ///< NaN in gaps.
    int tileCount = 0;
    int samplesPerTile = 0;
    int latMin = 0, latMax = 0, lonMin = 0, lonMax = 0; ///< Degrees.
};

/// "S12W077" -> {-12, -77}, case-insensitive.
std::optional<std::pair<int, int>> parseHgtName(std::string_view stem);

struct HgtTileFile {
    std::filesystem::path path;
    int lat = 0; ///< South-west corner, degrees.
    int lon = 0;
    int samples = 0; ///< From the file size.
};

/// Throws on a .hgt that is not square; empty if the directory does not exist.
std::vector<HgtTileFile> scanHgtDirectory(const std::filesystem::path& directory);

struct HgtTile {
    ElevationGrid grid;
    bool hasVoids = false;
};
HgtTile loadHgtTile(const HgtTileFile& file);

/// Throws on mixed resolutions or corrupt files.
std::optional<HgtMosaic> loadHgtMosaic(const std::filesystem::path& directory);

} // namespace orbis
