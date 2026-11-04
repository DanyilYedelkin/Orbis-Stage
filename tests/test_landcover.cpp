#include "core/terrain/ElevationGrid.hpp"
#include "core/terrain/LandCover.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;

const std::filesystem::path kLandCoverFile =
    std::filesystem::path(ORBIS_TEST_DATA_DIR) / "earth_landcover.olc";

/// Writes cells in the .olc format, the same way tools/prepare_landcover.py does.
std::vector<char> encode(int width, int height, const std::vector<std::uint8_t>& cells) {
    std::vector<char> out = {'O', 'L', 'C', '1'};
    const auto u32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
        }
    };
    u32(static_cast<std::uint32_t>(width));
    u32(static_cast<std::uint32_t>(height));
    u32(kModisClassCount);
    for (int r = 0; r < height; ++r) {
        int start = 0;
        for (int c = 1; c <= width; ++c) {
            const auto at = [&](int col) { return cells[static_cast<std::size_t>(r * width + col)]; };
            if (c == width || at(c) != at(start)) {
                out.push_back(static_cast<char>(at(start)));
                auto length = static_cast<std::uint32_t>(c - start);
                do {
                    const auto byte = static_cast<std::uint8_t>(length & 0x7Fu);
                    length >>= 7;
                    out.push_back(static_cast<char>(byte | (length ? 0x80u : 0u)));
                } while (length);
                start = c;
            }
        }
    }
    std::uint32_t h = 0x811C9DC5u;
    for (const std::uint8_t b : cells) {
        h = (h ^ b) * 0x01000193u;
    }
    u32(h);
    return out;
}

std::filesystem::path writeTemp(const std::string& name, const std::vector<char>& bytes) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

const LandCover& earthLandCover() {
    static const LandCover cover = [] {
        LandCover c = LandCover::load(kLandCoverFile);
        c.separateInlandWater(
            ElevationGrid::load(std::filesystem::path(ORBIS_TEST_DATA_DIR) / "earth_etopo10.oeg"));
        return c;
    }();
    return cover;
}

LandClass at(double latDeg, double lonDeg) { return earthLandCover().classAt(latDeg * kDeg, lonDeg * kDeg); }

bool isForest(LandClass c) {
    return c >= LandClass::EvergreenNeedleleafForest && c <= LandClass::MixedForest;
}
} // namespace

TEST_CASE("land cover file round-trips through the run-length format", "[landcover]") {
    // Long runs (multi-byte varints), single cells and every class value.
    const int width = 300;
    const int height = 3;
    std::vector<std::uint8_t> cells(static_cast<std::size_t>(width * height), 10);
    for (int c = 0; c < kModisClassCount; ++c) {
        cells[static_cast<std::size_t>(width + c * 3)] = static_cast<std::uint8_t>(c);
    }
    std::fill(cells.begin() + 2 * width + 10, cells.begin() + 2 * width + 250, std::uint8_t{0});

    const LandCover loaded = LandCover::load(writeTemp("orbis_lc_roundtrip.olc", encode(width, height, cells)));
    CHECK(loaded.width() == width);
    CHECK(loaded.height() == height);
    CHECK(loaded.cells() == cells);
}

TEST_CASE("corrupt land cover files are rejected with a message", "[landcover]") {
    const std::vector<std::uint8_t> cells(16 * 8, 12);
    const std::vector<char> good = encode(16, 8, cells);

    std::vector<char> badMagic = good;
    badMagic[0] = 'X';
    CHECK_THROWS_AS(LandCover::load(writeTemp("orbis_lc_magic.olc", badMagic)), std::runtime_error);

    std::vector<char> truncated(good.begin(), good.end() - 7);
    CHECK_THROWS_AS(LandCover::load(writeTemp("orbis_lc_trunc.olc", truncated)), std::runtime_error);

    std::vector<char> flipped = good;
    flipped[16] = static_cast<char>(13); // first run's class: decodes fine, checksum must catch it
    CHECK_THROWS_AS(LandCover::load(writeTemp("orbis_lc_crc.olc", flipped)), std::runtime_error);

    CHECK_THROWS_AS(LandCover::load("does/not/exist.olc"), std::runtime_error);
}

TEST_CASE("land cover cells are area-registered from the north-west corner", "[landcover]") {
    // 4 x 2 cells of 90 x 90 degrees: values 0..7, row 0 is the northern hemisphere.
    std::vector<std::uint8_t> cells = {1, 2, 3, 4, 5, 6, 7, 8};
    const LandCover lc(4, 2, cells);
    CHECK(lc.classAt(45 * kDeg, -135 * kDeg) == LandClass{1});
    CHECK(lc.classAt(45 * kDeg, 135 * kDeg) == LandClass{4});
    CHECK(lc.classAt(-45 * kDeg, -45 * kDeg) == LandClass{6});
    CHECK(lc.classAt(-89.9 * kDeg, 179.9 * kDeg) == LandClass{8});
    // Longitude wraps: +180 is -180, and beyond the date line continues on the other side.
    CHECK(lc.classAt(10 * kDeg, 180 * kDeg) == LandClass{1});
    CHECK(lc.classAt(10 * kDeg, (180 + 30) * kDeg) == LandClass{1});
    // Poles clamp instead of wrapping into the other hemisphere.
    CHECK(lc.classAt(90 * kDeg, 0) == LandClass{3});
    CHECK(lc.classAt(-90 * kDeg, 0) == LandClass{7});
    CHECK_THAT(lc.cellLatitude(0), WithinAbs(45 * kDeg, 1e-12));
    CHECK_THAT(lc.cellLongitude(3), WithinAbs(135 * kDeg, 1e-12));
}

TEST_CASE("water connected to deep sea stays ocean, isolated water becomes a lake", "[landcover]") {
    // 1-degree global elevation: deep ocean west of -100 deg, land (500 m) elsewhere.
    std::vector<float> nodes;
    for (int r = 0; r <= 180; ++r) {
        for (int c = 0; c <= 360; ++c) {
            nodes.push_back(c - 180 < -100 ? -4000.0f : 500.0f);
        }
    }
    const ElevationGrid elevation(361, 181, std::move(nodes));

    // 36 x 18 cells of 10 degrees. Land everywhere, plus:
    //  - an ocean strip over the deep area that extends as a "bay" onto the land (stays Ocean),
    //  - an isolated lake on land (becomes InlandWater),
    //  - a water strip across the date line, connected to the ocean through the wrap.
    const int w = 36;
    const int h = 18;
    std::vector<std::uint8_t> cells(static_cast<std::size_t>(w * h), 12);
    const auto set = [&](int r, int c, LandClass v) {
        cells[static_cast<std::size_t>(r * w + c)] = static_cast<std::uint8_t>(v);
    };
    for (int r = 0; r < h; ++r) {
        set(r, 2, LandClass::Ocean); // lon -160..-150: deep
    }
    set(9, 3, LandClass::Ocean);  // bay, on land, connected
    set(9, 4, LandClass::Ocean);
    set(5, 20, LandClass::Ocean); // lake, 20 deg east
    set(5, 21, LandClass::Ocean);
    set(12, 35, LandClass::Ocean); // across the date line from column 0...
    set(12, 0, LandClass::Ocean);
    set(12, 1, LandClass::Ocean); // ...and next to the deep strip at column 2

    LandCover lc(w, h, cells);
    CHECK(lc.separateInlandWater(elevation) == 2);
    CHECK(lc.cell(9, 4) == LandClass::Ocean);
    CHECK(lc.cell(12, 35) == LandClass::Ocean);
    CHECK(lc.cell(5, 20) == LandClass::InlandWater);
    CHECK(lc.cell(5, 21) == LandClass::InlandWater);
    CHECK(lc.cell(0, 0) == LandClass::Cropland);
}

TEST_CASE("real land cover matches known geography", "[landcover][earth]") {
    const LandCover& lc = earthLandCover();
    CHECK(lc.width() == 7200);
    CHECK(lc.height() == 3600);

    CHECK(at(23.0, 13.0) == LandClass::Barren);                     // Sahara (Libya / Niger)
    CHECK(at(72.0, -40.0) == LandClass::SnowAndIce);                // Greenland ice sheet
    CHECK(at(-80.0, 0.0) == LandClass::SnowAndIce);                 // Antarctica
    CHECK(at(0.0, 20.0) == LandClass::EvergreenBroadleafForest);    // Congo rainforest
    CHECK(at(-6.0, -64.0) == LandClass::EvergreenBroadleafForest);  // Amazon
    CHECK(at(0.0, -150.0) == LandClass::Ocean);                     // Pacific
    CHECK(at(48.86, 2.35) == LandClass::Urban);                     // Paris
    CHECK(at(-25.0, 133.0) == LandClass::OpenShrubland);            // Australian outback
    CHECK((isForest(at(61.0, 90.0)) || at(61.0, 90.0) == LandClass::WoodySavanna)); // taiga

    // Lakes are separated from the sea; the sea next to them is not.
    CHECK(at(46.45, 6.55) == LandClass::InlandWater);  // Lake Geneva
    CHECK(at(-1.0, 33.0) == LandClass::InlandWater);   // Lake Victoria
    CHECK(at(43.5, 7.5) == LandClass::Ocean);          // Ligurian Sea off Nice

    // Global statistics: about 2/3 of the CELLS are sea (cells shrink towards the poles, so this
    // is not the 71 % area fraction), and some lakes exist.
    const auto f = lc.classFractions();
    CHECK(f[0] > 0.55);
    CHECK(f[0] < 0.75);
    CHECK(f[static_cast<std::size_t>(LandClass::InlandWater)] > 0.002);
    CHECK(f[static_cast<std::size_t>(LandClass::InlandWater)] < 0.03);
}
