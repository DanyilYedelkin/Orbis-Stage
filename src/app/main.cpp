#include "app/Application.hpp"

#include <cstdio>
#include <exception>
#include <iostream>
#include <string_view>

namespace {

void printUsage() {
    std::cout << "Usage: orbis [--world earth|procedural] [--elevation file.oeg] [--size WxH]\n"
                 "             [--view lat,lon,altKm,heading,pitch [--above-ground]] [--sun az,el]\n"
                 "             [--fov degrees] [--lod-colors] [--no-geomorph] [--no-shadows]\n"
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
