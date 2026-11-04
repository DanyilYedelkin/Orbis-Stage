#pragma once

#include "core/terrain/ElevationGrid.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orbis {

/// SRTM ".hgt" tiles - the de-facto exchange format for high-resolution elevation (SRTM, and the
/// void-filled mountain DEMs of viewfinderpanoramas.org, which are excellent for the Alps).
///
/// One file = one 1 x 1 degree square named after its south-west corner ("N45E007.hgt"):
/// N x N big-endian int16 metres, rows north -> south, columns west -> east, grid-registered (the
/// edges are shared with the neighbours). N = 1201 (3 arc-seconds, ~90 m) or 3601 (1", ~30 m).
/// -32768 marks a void.
struct HgtMosaic {
    ElevationGrid grid; ///< Regional grid over the bounding box of all tiles; gaps are NaN.
    int tileCount = 0;
    int samplesPerTile = 0; ///< N
    int latMin = 0, latMax = 0, lonMin = 0, lonMax = 0; ///< Degrees, bounding box.
};

/// "N45E007" -> {45, 7}, "S12W077" -> {-12, -77}. Case-insensitive; nullopt if not a tile name.
std::optional<std::pair<int, int>> parseHgtName(std::string_view stem);

/// A tile file found on disk (not loaded).
struct HgtTileFile {
    std::filesystem::path path;
    int lat = 0; ///< South-west corner, degrees.
    int lon = 0;
    int samples = 0; ///< N, from the file size.
};

/// Every *.hgt in `directory` with a valid name and a square size. Throws std::runtime_error on a
/// file that is not a square tile; an empty list if the directory does not exist.
std::vector<HgtTileFile> scanHgtDirectory(const std::filesystem::path& directory);

/// One tile as a regional grid over its 1 x 1 degree square (NaN at voids).
struct HgtTile {
    ElevationGrid grid;
    bool hasVoids = false;
};
/// Throws std::runtime_error on a truncated file.
HgtTile loadHgtTile(const HgtTileFile& file);

/// Loads every *.hgt in `directory` into one seamless mosaic. nullopt if there are none.
/// Throws std::runtime_error on inconsistent or corrupt files (mixed resolutions, bad sizes).
std::optional<HgtMosaic> loadHgtMosaic(const std::filesystem::path& directory);

} // namespace orbis
