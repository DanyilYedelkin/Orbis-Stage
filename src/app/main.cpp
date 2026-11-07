#include "app/Application.hpp"
#include "core/geo/Ellipsoid.hpp"

#include <cmath>
#include <numbers>

#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

/// Prints famous Alpine summits and valleys: real height vs this terrain. The quickest way to see
/// what high-resolution tiles change (and to check they were loaded correctly).
int checkAlps(const orbis::AppOptions& options) {
    const orbis::Ellipsoid ellipsoid = orbis::Ellipsoid::wgs84();
    std::shared_ptr<orbis::HgtTileStreamer> tiles;
    const auto source = orbis::makeElevationSource(options, ellipsoid, &tiles);
    struct Place {
        const char* name;
        double lat, lon, real;
    };
    const Place places[] = {
        {"Mont Blanc", 45.8326, 6.8652, 4808},   {"Monte Rosa", 45.9369, 7.8668, 4634},
        {"Matterhorn", 45.9763, 7.6586, 4478},   {"Jungfrau", 46.5368, 7.9626, 4158},
        {"Eiger", 46.5775, 8.0053, 3967},        {"Chamonix (valley)", 45.9237, 6.8694, 1035},
        {"Zermatt (valley)", 46.0207, 7.7491, 1608}, {"Grindelwald (valley)", 46.6242, 8.0414, 1034},
    };
    constexpr double kDeg = std::numbers::pi / 180.0;
    std::printf("%-22s %7s %7s %7s\n", "", "real", "Orbis", "diff");
    for (const Place& p : places) {
        if (tiles) {
            // Tiles stream on demand; here, load the one under each place right away - and its
            // neighbours, since the data fades out towards squares that are not loaded.
            const int lat = static_cast<int>(std::floor(p.lat));
            const int lon = static_cast<int>(std::floor(p.lon));
            for (int dLat = -1; dLat <= 1; ++dLat) {
                for (int dLon = -1; dLon <= 1; ++dLon) {
                    tiles->loadNow(lat + dLat, lon + dLon);
                }
            }
        }
        const double h = source->elevation(
            orbis::normalize(ellipsoid.geodeticToEcef({p.lat * kDeg, p.lon * kDeg, 0.0})));
        std::printf("%-22s %7.0f %7.0f %+7.0f\n", p.name, p.real, h, h - p.real);
    }
    return 0;
}

void printUsage() {
    std::cout << "Usage: orbis [--world earth|procedural] [--elevation file.oeg] [--hgt dir [--tile-budget MB]]\n"
                 "             [--landcover file.olc | --no-landcover] [--check-alps] [--size WxH]\n"
                 "             [--view lat,lon,altKm,heading,pitch [--above-ground]] [--sun az,el]\n"
                 "             [--fov degrees] [--lod-colors] [--no-geomorph] [--no-shadows] [--no-detail]\n"
                 "             [--clouds coverage | --no-clouds] [--date MM-DD]\n"
                 "             [--screenshot file.ppm [--show-ui] [--record frames,metres]\n"
                 "             [--record-lod-only]]\n"
                 "Controls: RMB + mouse look | WASD move | Space/E up | Ctrl/Q down |\n"
                 "          Shift boost | wheel speed | [ ] detail | F1 wireframe |\n"
                 "          F2 freeze LOD | F3 LOD colours | F4 panel | Esc quit\n";
}

} // namespace

int main(int argc, char** argv) {
    orbis::AppOptions options;
    bool showUiFlag = false;
    bool checkAlpsFlag = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) {
            options.screenshotPath = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &options.width, &options.height) != 2 ||
                options.width <= 0 || options.height <= 0) {
                std::cerr << "Invalid --size, expected e.g. 1920x1080\n";
                return 1;
            }
        } else if (arg == "--view" && i + 1 < argc) {
            auto& v = options.view;
            if (std::sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf", &v.latitude, &v.longitude,
                            &v.altitudeKm, &v.heading, &v.pitch) != 5) {
                std::cerr << "Invalid --view, expected e.g. 48.72,21.26,500,0,-30\n";
                return 1;
            }
        } else if (arg == "--sun" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%lf,%lf", &options.sunAzimuth, &options.sunElevation) != 2) {
                std::cerr << "Invalid --sun, expected azimuth,elevation e.g. 225,30\n";
                return 1;
            }
        } else if (arg == "--world" && i + 1 < argc) {
            const std::string_view world = argv[++i];
            if (world == "earth") {
                options.world = orbis::World::Earth;
            } else if (world == "procedural") {
                options.world = orbis::World::Procedural;
            } else {
                std::cerr << "Invalid --world, expected earth or procedural\n";
                return 1;
            }
        } else if (arg == "--hgt" && i + 1 < argc) {
            options.hgtDirectory = argv[++i];
        } else if (arg == "--landcover" && i + 1 < argc) {
            options.landCoverFile = argv[++i];
        } else if (arg == "--no-landcover") {
            options.landCover = false;
        } else if (arg == "--tile-budget" && i + 1 < argc) {
            long long mb = 0;
            if (std::sscanf(argv[++i], "%lld", &mb) != 1 || mb < 16) {
                std::cerr << "Invalid --tile-budget, expected megabytes >= 16\n";
                return 1;
            }
            options.tileBudgetMb = static_cast<std::size_t>(mb);
        } else if (arg == "--check-alps") {
            checkAlpsFlag = true;
        } else if (arg == "--elevation" && i + 1 < argc) {
            options.elevationFile = argv[++i];
        } else if (arg == "--fov" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%lf", &options.fovDegrees) != 1 || options.fovDegrees < 5.0 ||
                options.fovDegrees > 120.0) {
                std::cerr << "Invalid --fov, expected degrees between 5 and 120\n";
                return 1;
            }
        } else if (arg == "--above-ground") {
            options.viewAboveGround = true;
        } else if (arg == "--no-clouds") {
            options.clouds = false;
        } else if (arg == "--clouds" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%lf", &options.cloudCoverage) != 1 ||
                options.cloudCoverage < 0.0 || options.cloudCoverage > 1.0) {
                std::cerr << "Invalid --clouds, expected a coverage between 0 and 1\n";
                return 1;
            }
        } else if (arg == "--date" && i + 1 < argc) {
            int month = 0;
            int day = 0;
            try {
                if (std::sscanf(argv[++i], "%d-%d", &month, &day) != 2) {
                    throw std::invalid_argument("format");
                }
                options.dayOfYear = orbis::dayOfYear(month, day);
            } catch (const std::invalid_argument&) {
                std::cerr << "Invalid --date, expected MM-DD e.g. 01-15\n";
                return 1;
            }
        } else if (arg == "--no-detail") {
            options.surfaceDetail = false;
        } else if (arg == "--no-shadows") {
            options.shadows = false;
        } else if (arg == "--no-geomorph") {
            options.geomorph = false;
        } else if (arg == "--record" && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%d,%lf", &options.recordFrames, &options.recordStep) != 2 ||
                options.recordFrames <= 0) {
                std::cerr << "Invalid --record, expected frames,metresPerFrame e.g. 40,500\n";
                return 1;
            }
        } else if (arg == "--record-lod-only") {
            options.recordLodOnly = true;
        } else if (arg == "--show-ui") {
            showUiFlag = true;
        } else if (arg == "--lod-colors") {
            options.showLodLevels = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage();
            return 1;
        }
    }

    if (checkAlpsFlag) {
        try {
            return checkAlps(options);
        } catch (const std::exception& e) {
            std::cerr << "Fatal: " << e.what() << '\n';
            return 1;
        }
    }

    // The panel is on in interactive use; screenshots are clean unless asked for.
    options.showUi = !options.screenshotPath || showUiFlag;

    try {
        orbis::Application app(options);
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 1;
    }
}
