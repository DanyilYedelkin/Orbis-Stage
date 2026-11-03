#include "app/Application.hpp"

#include "app/DataFiles.hpp"
#include "app/gl/DebugOutput.hpp"
#include "core/terrain/CubeSphere.hpp"
#include "core/terrain/EarthTerrain.hpp"
#include "core/terrain/HeightField.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <fstream>
#include <thread>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace orbis {

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr std::size_t kUploadsPerFrame = 24; // bounds GPU upload work per frame (no hitches)

WindowConfig makeWindowConfig(const AppOptions& options) {
    WindowConfig config;
    config.width = options.width;
    config.height = options.height;
    config.title = "Orbis";
    config.visible = !options.screenshotPath.has_value();
#ifndef NDEBUG
    config.debugContext = true;
#endif
    return config;
}

constexpr std::uint64_t kTerrainSeed = 20261102;
constexpr const char* kEarthDataFile = "earth_etopo10.oeg";

std::unique_ptr<const ElevationSource> makeElevationSource(const AppOptions& options,
                                                           const Ellipsoid& ellipsoid) {
    if (options.world == World::Earth) {
        const std::optional<std::filesystem::path> path =
            options.elevationFile ? std::optional(std::filesystem::path(*options.elevationFile))
                                  : findDataFile(kEarthDataFile);
        if (path) {
            try {
                const auto start = std::chrono::steady_clock::now();
                auto earth = std::make_unique<EarthTerrain>(ElevationGrid::load(*path), ellipsoid,
                                                            kTerrainSeed);
                const auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start);
                std::cout << "Earth elevation: " << path->string() << " ("
                          << static_cast<long long>(ms.count()) << " ms)\n";
                return earth;
            } catch (const std::exception& e) {
                std::cerr << "Warning: " << e.what() << '\n';
            }
        } else {
            std::cerr << "Warning: " << kEarthDataFile << " not found (run tools/prepare_etopo.py "
                      << "or pass --elevation).\n";
        }
        std::cerr << "Falling back to the procedural planet.\n";
    }
    return std::make_unique<HeightField>(kTerrainSeed);
}

} // namespace

Application::Application(const AppOptions& options)
    : options_(options), window_(makeWindowConfig(options)), ui_(window_.handle()),
      elevation_(makeElevationSource(options, ellipsoid_)),
      isEarth_(dynamic_cast<const EarthTerrain*>(elevation_.get()) != nullptr),
      terrain_(ellipsoid_, *elevation_, LodSettings{}) {
    gl::enableDebugOutput();

    std::cout << "OpenGL " << glGetString(GL_VERSION) << " | " << glGetString(GL_RENDERER) << '\n';

    // Reversed-Z setup (see perspectiveReversedZInfinite): depth range [0, 1], 0 = infinitely
    // far, so the buffer is cleared to 0 and nearer fragments win with GREATER.
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glClearDepth(0.0);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    drawSettings_.showLodLevels = options.showLodLevels;

    const AppOptions::View& v = options_.view;
    camera_.setPosition({v.latitude * kDegToRad, v.longitude * kDegToRad, v.altitudeKm * 1000.0});
    camera_.setOrientation(v.heading * kDegToRad, v.pitch * kDegToRad);

    showUi_ = options.showUi;
    drawSettings_.geomorph = options.geomorph;
    sunAzimuth_ = options.sunAzimuth;
    sunElevation_ = options.sunElevation;
    setSun(v.latitude * kDegToRad, v.longitude * kDegToRad);
}

void Application::setSun(double latitude, double longitude) {
    // Sun given in the LOCAL frame (azimuth from north, elevation above the horizon), converted
    // to a world (ECEF) direction - so "afternoon light from the south-west" works anywhere.
    const EnuFrame enu = Ellipsoid::enuFrame(latitude, longitude);
    const double az = sunAzimuth_ * kDegToRad;
    const double el = sunElevation_ * kDegToRad;
    drawSettings_.sunDirection =
        normalize((enu.north * std::cos(az) + enu.east * std::sin(az)) * std::cos(el) +
                  enu.up * std::sin(el));
}

void Application::goTo(const AppOptions::View& view, double sunAzimuth, double sunElevation) {
    camera_.setPosition({view.latitude * kDegToRad, view.longitude * kDegToRad,
                         view.altitudeKm * 1000.0});
    camera_.setOrientation(view.heading * kDegToRad, view.pitch * kDegToRad);
    sunAzimuth_ = sunAzimuth;
    sunElevation_ = sunElevation;
    setSun(view.latitude * kDegToRad, view.longitude * kDegToRad);
    frozenLodCamera_.reset();
}

void Application::toggleFreeze() {
    if (frozenLodCamera_) {
        frozenLodCamera_.reset();
    } else {
        frozenLodCamera_ = camera_;
    }
}

int Application::run() {
    if (options_.screenshotPath) {
        settleTerrain();
        const TerrainStats& st = terrain_.stats();
        std::cout << st.drawnChunks << " chunks drawn (" << st.drawnTriangles << " triangles), "
                  << st.residentChunks << " resident, deepest level " << st.deepestLevel
                  << ", culled " << st.frustumCulled << " frustum / " << st.horizonCulled
                  << " horizon\n";

        if (options_.recordFrames > 0) {
            // Fly forward and save every frame, with the LOD fully settled each time: any change
            // between consecutive frames then comes from motion and LOD transitions only, never
            // from loading - which is what makes popping measurable.
            std::string base = *options_.screenshotPath;
            if (const auto dot = base.rfind('.'); dot != std::string::npos) {
                base.erase(dot);
            }
            for (int frame = 0; frame < options_.recordFrames; ++frame) {
                if (frame > 0) {
                    if (options_.recordLodOnly) {
                        if (!frozenLodCamera_) {
                            frozenLodCamera_ = camera_;
                        }
                        frozenLodCamera_->translate(frozenLodCamera_->basis().forward *
                                                    options_.recordStep);
                    } else {
                        camera_.translate(camera_.basis().forward * options_.recordStep);
                    }
                    settleTerrain();
                }
                renderFrame();
                char suffix[16];
                std::snprintf(suffix, sizeof(suffix), "_%03d.ppm", frame);
                saveScreenshot(base + suffix);
            }
            return 0;
        }

        if (showUi_) {
            // ImGui sizes auto-resizing windows over the first frames; let it settle.
            for (int i = 0; i < 3; ++i) {
                ui_.beginFrame();
                uiFrameActive_ = true;
                buildDebugPanel(1.0 / 60.0);
                renderFrame();
            }
        } else {
            renderFrame();
        }
        saveScreenshot(*options_.screenshotPath);
        return 0;
    }

    double previous = Window::time();
    while (!window_.shouldClose()) {
        Window::pollEvents();

        const double now = Window::time();
        // Clamp dt: after a breakpoint or window drag, a huge dt would teleport the camera.
        const double dt = std::min(now - previous, 0.1);
        previous = now;

        if (showUi_) {
            ui_.beginFrame();
            uiFrameActive_ = true;
            buildDebugPanel(dt);
        }
        handleInput(dt);
        updateTerrain(kUploadsPerFrame);
        renderFrame();
        window_.swapBuffers();
        updateTitle(dt);
    }
    return 0;
}

void Application::handleInput(double dt) {
    GLFWwindow* w = window_.handle();
    const auto down = [w](int key) { return glfwGetKey(w, key) == GLFW_PRESS; };

    // While the panel has the mouse or keyboard (dragging a slider, typing), the camera must
    // not react to the same input.
    const bool uiMouse = showUi_ && ui_.wantsMouse();
    const bool uiKeyboard = showUi_ && ui_.wantsKeyboard();
    if (uiKeyboard) {
        return;
    }

    if (down(GLFW_KEY_ESCAPE)) {
        window_.requestClose();
    }
    if (uiKey_.pressed(down(GLFW_KEY_F4))) {
        showUi_ = !showUi_;
    }

    if (wireframeKey_.pressed(down(GLFW_KEY_F1))) {
        drawSettings_.wireframe = !drawSettings_.wireframe;
    }
    if (freezeKey_.pressed(down(GLFW_KEY_F2))) {
        toggleFreeze();
    }
    if (lodColorKey_.pressed(down(GLFW_KEY_F3))) {
        drawSettings_.showLodLevels = !drawSettings_.showLodLevels;
    }
    // [ = more detail, ] = less detail.
    double& pixelError = terrain_.lodSettings().maxPixelError;
    if (finerKey_.pressed(down(GLFW_KEY_LEFT_BRACKET))) {
        pixelError = std::max(pixelError / 1.5, 0.5);
    }
    if (coarserKey_.pressed(down(GLFW_KEY_RIGHT_BRACKET))) {
        pixelError = std::min(pixelError * 1.5, 64.0);
    }

    // Mouse look while the right button is held.
    // Start looking only if the click was not on the panel (but keep looking once started).
    const bool rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
                     (looking_ || !uiMouse);
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(w, &cx, &cy);
    if (rmb && !looking_) {
        window_.setCursorCaptured(true);
        lastCursorX_ = cx;
        lastCursorY_ = cy;
    } else if (!rmb && looking_) {
        window_.setCursorCaptured(false);
    }
    looking_ = rmb;
    if (looking_) {
        constexpr double kSensitivity = 0.0025; // radians per pixel
        camera_.rotate((cx - lastCursorX_) * kSensitivity, -(cy - lastCursorY_) * kSensitivity);
        lastCursorX_ = cx;
        lastCursorY_ = cy;
    }

    // Each wheel notch changes speed by 25%: multiplicative feels linear to humans.
    const double scroll = window_.consumeScroll();
    if (!uiMouse) {
        speedMultiplier_ = std::clamp(speedMultiplier_ * std::pow(1.25, scroll), 0.001, 50.0);
    }

    // Speed proportional to altitude: 1 km/s feels right at 1 km, 1000 km/s at 1000 km. With a
    // fixed speed you either crawl in orbit or crash into mountains at ground level.
    const CameraBasis b = camera_.basis();
    const double altitude = std::max(b.geodetic.height, 1.0);
    double speed = altitude * speedMultiplier_;
    if (down(GLFW_KEY_LEFT_SHIFT)) {
        speed *= 4.0;
    }

    Vec3d move{};
    if (down(GLFW_KEY_W)) move += b.forward;
    if (down(GLFW_KEY_S)) move -= b.forward;
    if (down(GLFW_KEY_D)) move += b.right;
    if (down(GLFW_KEY_A)) move -= b.right;
    if (down(GLFW_KEY_SPACE) || down(GLFW_KEY_E)) move += b.enu.up;
    if (down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_Q)) move -= b.enu.up;

    if (lengthSquared(move) > 0.0) {
        camera_.translate(normalize(move) * (speed * dt));
    }
}

void Application::settleTerrain() {
    // Let the LOD stream in until the image is final (or give up after a minute), so CI
    // screenshots are deterministic rather than "whatever had loaded so far".
    const auto start = std::chrono::steady_clock::now();
    do {
        updateTerrain(std::numeric_limits<std::size_t>::max());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (!terrain_.settled() &&
             std::chrono::steady_clock::now() - start < std::chrono::seconds(60));
}

void Application::updateTerrain(std::size_t maxUploads) {
    int width = 0;
    int height = 0;
    window_.framebufferSize(width, height);
    if (width == 0 || height == 0) {
        return;
    }
    terrain_.update(frozenLodCamera_ ? *frozenLodCamera_ : camera_, width, height, maxUploads);
}

void Application::renderFrame() {
    int width = 0;
    int height = 0;
    window_.framebufferSize(width, height);
    if (width == 0 || height == 0) {
        if (uiFrameActive_) {
            ui_.discardFrame();
            uiFrameActive_ = false;
        }
        return; // minimised
    }
    renderTarget_.resize(width, height);
    renderTarget_.bind();

    glClearColor(0.004f, 0.006f, 0.014f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    sky_.draw(camera_, aspect, drawSettings_.sunDirection);
    terrain_.draw(camera_, aspect, drawSettings_);
    if (uiFrameActive_) {
        ui_.render(); // into our framebuffer, so screenshots include it
        uiFrameActive_ = false;
    }

    renderTarget_.blitToWindow();
}

void Application::updateTitle(double dt) {
    titleTimer_ += dt;
    ++framesSinceTitle_;
    if (titleTimer_ < 0.25) {
        return;
    }

    const Geodetic g = camera_.basis().geodetic;
    const double fps = framesSinceTitle_ / titleTimer_;
    const double latDeg = g.latitude / kDegToRad;
    const double lonDeg = g.longitude / kDegToRad;

    const TerrainStats& st = terrain_.stats();

    char title[384];
    std::snprintf(title, sizeof(title),
                  "Orbis | %.0f fps | alt %.3f km | %.4f%c %.4f%c | %zu chunks (%.2fM tris), "
                  "lvl %d | queue %zu | err %.1fpx | speed x%.3f%s%s",
                  fps, g.height / 1000.0, std::abs(latDeg), latDeg >= 0 ? 'N' : 'S',
                  std::abs(lonDeg), lonDeg >= 0 ? 'E' : 'W', st.drawnChunks,
                  static_cast<double>(st.drawnTriangles) / 1e6, st.deepestLevel,
                  st.pendingBuilds + st.runningBuilds, terrain_.lodSettings().maxPixelError,
                  speedMultiplier_, drawSettings_.wireframe ? " | wireframe" : "",
                  frozenLodCamera_ ? " | LOD FROZEN" : "");
    window_.setTitle(title);

    titleTimer_ = 0.0;
    framesSinceTitle_ = 0;
}

void Application::saveScreenshot(const std::string& path) const {
    const std::vector<std::uint8_t> rgb = renderTarget_.readRgb();
    const int w = renderTarget_.width();
    const int h = renderTarget_.height();

    // Binary PPM: trivial format, no library needed. OpenGL rows are bottom-up, PPM top-down.
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot write screenshot: " + path);
    }
    out << "P6\n" << w << ' ' << h << "\n255\n";
    const auto rowBytes = static_cast<std::size_t>(w) * 3;
    for (int y = h - 1; y >= 0; --y) {
        out.write(reinterpret_cast<const char*>(rgb.data() + static_cast<std::size_t>(y) * rowBytes),
                  static_cast<std::streamsize>(rowBytes));
    }
    if (options_.recordFrames == 0) {
        std::cout << "Screenshot saved to " << path << '\n';
    }
}

} // namespace orbis
