#include "app/Application.hpp"

#include <cstdio>
#include <exception>
#include <iostream>
#include <string_view>

namespace {

void printUsage() {
    std::cout << "Usage: orbis [--size WxH] [--view lat,lon,altKm,heading,pitch] "
                 "[--screenshot file.ppm]\n"
                 "Controls: RMB + mouse look | WASD move | Space/E up | Ctrl/Q down |\n"
                 "          Shift boost | wheel speed | F1 wireframe | Esc quit\n";
}

} // namespace

int main(int argc, char** argv) {
    orbis::AppOptions options;

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
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage();
            return 1;
        }
    }

    try {
        orbis::Application app(options);
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 1;
    }
}
