#pragma once
// Shared by the .hgt tests: writing synthetic tiles whose height is a known function of latitude
// and longitude, so a swapped axis, a mirrored tile or the wrong byte order give wrong numbers.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>

namespace orbis::test {

namespace fs = std::filesystem;

/// Writes an SRTM-style tile: N x N big-endian int16, row 0 = NORTH edge, column 0 = WEST edge.
inline void writeTile(const fs::path& dir, int lat, int lon, int n,
               const std::function<double(double, double)>& f) {
    char name[32];
    std::snprintf(name, sizeof(name), "%c%02d%c%03d.hgt", lat >= 0 ? 'N' : 'S', std::abs(lat),
                  lon >= 0 ? 'E' : 'W', std::abs(lon));
    std::ofstream out(dir / name, std::ios::binary);
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const double la = lat + 1.0 - static_cast<double>(r) / (n - 1);
            const double lo = lon + static_cast<double>(c) / (n - 1);
            const double value = f(la, lo);
            const auto v = static_cast<std::uint16_t>(
                static_cast<std::int16_t>(std::isnan(value) ? -32768 : std::lround(value)));
            const char bytes[2] = {static_cast<char>(v >> 8), static_cast<char>(v & 0xFF)};
            out.write(bytes, 2);
        }
    }
}

/// A fresh temporary directory per test.
inline fs::path freshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}


} // namespace orbis::test
