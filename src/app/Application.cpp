#include "app/Application.hpp"

#include "app/DataFiles.hpp"
#include "core/terrain/CubeSphere.hpp"
#include "core/terrain/EarthTerrain.hpp"
#include "core/terrain/HeightField.hpp"
#include "core/terrain/HgtTileStreamer.hpp"
#include "core/terrain/HgtTiles.hpp"
#include "core/terrain/LandCover.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
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
#include <string>
#include <vector>

namespace orbis {

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr std::size_t kUploadsPerFrame = 24;
constexpr int kCloudConvergenceFrames = 12;

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
// The 1' grid is generated locally (tools/prepare_etopo2022.py); 10' ships with the repository.
constexpr std::array<const char*, 2> kEarthDataFiles = {"earth_etopo2022_60s.oeg",
                                                        "earth_etopo10.oeg"};
constexpr const char* kLandCoverFile = "earth_landcover.olc";

} // namespace

std::unique_ptr<const ElevationSource> makeElevationSource(const AppOptions& options,
                                                           const Ellipsoid& ellipsoid,
                                                           std::shared_ptr<HgtTileStreamer>* tiles) {
    if (options.world == World::Earth) {
        std::optional<std::filesystem::path> path;
        if (options.elevationFile) {
            path = std::filesystem::path(*options.elevationFile);
        }
        for (const char* name : kEarthDataFiles) {
            path = path ? path : findDataFile(name);
        }
        if (path) {
            try {
                const auto start = std::chrono::steady_clock::now();
                // Only indexed here; tiles stream in around the camera.
                std::shared_ptr<HgtTileStreamer> streamer;
                const std::optional<std::filesystem::path> hgt =
                    options.hgtDirectory ? std::optional(std::filesystem::path(*options.hgtDirectory))
                                         : findDataDirectory("hgt");
                if (hgt) {
                    std::vector<HgtTileFile> files = scanHgtDirectory(*hgt);
                    if (!files.empty()) {
                        std::size_t oneArcSecond = 0;
                        for (const HgtTileFile& f : files) {
                            oneArcSecond += f.samples > 2000 ? 1 : 0;
                        }
                        std::cout << "High-resolution tiles: " << files.size() << " in " << hgt->string()
                                  << " (" << oneArcSecond << " at 1\", the rest 3\"), streamed on demand"
                                  << ", budget " << (options.tileBudgetMb) << " MB\n";
                        TileStreamingSettings settings;
                        settings.memoryBudget = options.tileBudgetMb << 20;
                        streamer = std::make_shared<HgtTileStreamer>(std::move(files), settings);
                    }
                }
                if (tiles != nullptr) {
                    *tiles = streamer;
                }
                auto earth = std::make_unique<EarthTerrain>(ElevationGrid::load(*path), ellipsoid,
                                                            kTerrainSeed, std::move(streamer));
                const auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start);
                const ElevationGrid& grid = earth->measuredGrid();
                std::cout << "Earth elevation: " << path->string() << ", " << grid.width() << " x "
                          << grid.height() << " (" << 21600.0 / (grid.width() - 1) << "'), "
                          << static_cast<long long>(ms.count()) << " ms\n";
                return earth;
            } catch (const std::exception& e) {
                std::cerr << "Warning: " << e.what() << '\n';
            }
        } else {
            std::cerr << "Warning: no Earth elevation grid found (run tools/prepare_etopo.py or "
                      << "pass --elevation).\n";
        }
        std::cerr << "Falling back to the procedural planet.\n";
    }
    return std::make_unique<HeightField>(kTerrainSeed);
}

namespace {
Climate makeClimate(const ElevationSource& elevation, const Ellipsoid& ellipsoid) {
    const auto start = std::chrono::steady_clock::now();
    Climate climate([&](double latitude, double longitude) {
        const Vec3d dir = normalize(ellipsoid.geodeticToEcef({latitude, longitude, 0.0}));
        return elevation.elevation(dir, 50000.0) < 0.0;
    });
    const auto ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    std::cout << "Climate: oceanicity map " << Climate::kMapWidth << " x " << Climate::kMapHeight
              << " (" << static_cast<long long>(ms.count()) << " ms)\n";
    return climate;
}
} // namespace

Application::Application(const AppOptions& options)
    : options_(options), window_(makeWindowConfig(options)), ui_(window_.handle()),
      elevation_(makeElevationSource(options, ellipsoid_, &tiles_)),
      isEarth_(dynamic_cast<const EarthTerrain*>(elevation_.get()) != nullptr),
      climate_(makeClimate(*elevation_, ellipsoid_)),
      atmosphere_(AtmosphereParameters{}, ellipsoid_), sky_(atmosphere_),
      clouds_(CloudParameters{.coverage = options.cloudCoverage}, atmosphere_, ellipsoid_),
      terrain_(ellipsoid_, *elevation_, LodSettings{}, atmosphere_, clouds_, climate_) {
    const double lutError = atmosphere_.validateAgainstCpu();
    std::cout << "Atmosphere LUTs ready; transmittance GPU vs CPU max difference " << lutError
              << '\n';
    if (lutError > 0.01) {
        throw std::runtime_error("Atmosphere transmittance LUT disagrees with the CPU reference");
    }
    const double irradianceError = atmosphere_.irradianceValidationError();
    std::cout << "Sky irradiance LUT: single scattering GPU vs CPU max difference "
              << irradianceError * 100.0 << " % of the maximum; multiple scattering adds "
              << (atmosphere_.multipleScatteringGain() - 1.0) * 100.0 << " % at noon\n";
    if (irradianceError > 0.03) {
        throw std::runtime_error("Sky irradiance LUT disagrees with the CPU reference");
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION) << " | " << glGetString(GL_RENDERER) << '\n';

    // Reversed-Z: 0 is infinitely far.
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glClearDepth(0.0);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    drawSettings_.showLodLevels = options.showLodLevels;
    if (isEarth_ && options.landCover) {
        loadLandCover();
    }

    const AppOptions::View& v = options_.view;
    double altitude = v.altitudeKm * 1000.0;
    if (options.viewAboveGround) {
        altitude += groundHeightAt(ellipsoid_.geodeticToEcef(
            {v.latitude * kDegToRad, v.longitude * kDegToRad, 0.0}));
    }
    camera_.setPosition({v.latitude * kDegToRad, v.longitude * kDegToRad, altitude});
    camera_.setOrientation(v.heading * kDegToRad, v.pitch * kDegToRad);

    showUi_ = options.showUi;
    camera_.verticalFov = static_cast<float>(options.fovDegrees * kDegToRad);
    drawSettings_.geomorph = options.geomorph;
    drawSettings_.shadows = options.shadows;
    drawSettings_.surfaceDetail = options.surfaceDetail;
    drawSettings_.dayOfYear = static_cast<float>(options.dayOfYear);
    // Popping measurements diff consecutive frames; temporal cloud noise would swamp them.
    drawSettings_.clouds = options.clouds && options.recordFrames == 0;
    sunAzimuth_ = options.sunAzimuth;
    sunElevation_ = options.sunElevation;
    setSun(v.latitude * kDegToRad, v.longitude * kDegToRad);
}

void Application::loadLandCover() {
    const std::optional<std::filesystem::path> path =
        options_.landCoverFile ? std::optional(std::filesystem::path(*options_.landCoverFile))
                               : findDataFile(kLandCoverFile);
    if (!path) {
        std::cerr << "Warning: " << kLandCoverFile << " not found (run tools/prepare_landcover.py"
                  << " or pass --landcover); colours are stylised by latitude.\n";
        return;
    }
    try {
        const auto start = std::chrono::steady_clock::now();
        LandCover cover = LandCover::load(*path);
        const auto* earth = dynamic_cast<const EarthTerrain*>(elevation_.get());
        const std::size_t lakes = cover.separateInlandWater(earth->measuredGrid());
        landCover_.emplace(cover);
        terrain_.setLandCover(&*landCover_);
        const auto ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start);
        std::cout << "Land cover: " << path->string() << ", " << cover.width() << " x "
                  << cover.height() << " cells, " << lakes << " lake cells ("
                  << static_cast<long long>(ms.count()) << " ms)\n";
    } catch (const std::exception& e) {
        std::cerr << "Warning: " << e.what() << "; colours are stylised by latitude.\n";
    }
}

void Application::setSun(double latitude, double longitude) {
    // Azimuth / elevation are local to the given position.
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
    clouds_.resetHistory();
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
        if (tiles_) {
            const HgtTileStreamer::Stats ts = tiles_->stats();
            std::cout << "hgt tiles: " << ts.loaded << " of " << ts.available << " loaded on demand ("
                      << ts.bytes / (1024 * 1024) << " MB), " << ts.evictions << " evicted\n";
        }

        if (options_.recordFrames > 0) {
            // Settled every frame, so differences come from motion and LOD transitions only.
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

        // Clouds accumulate over frames and ImGui sizes its windows over the first few.
        const int frames = drawSettings_.clouds ? kCloudConvergenceFrames : (showUi_ ? 3 : 1);
        for (int i = 0; i < frames; ++i) {
            if (showUi_) {
                ui_.beginFrame();
                uiFrameActive_ = true;
                buildDebugPanel(1.0 / 60.0);
            }
            renderFrame();
        }
        gpuTimers_.finishFrame();
        printGpuTimes();
        saveScreenshot(*options_.screenshotPath);
        return 0;
    }

    double previous = Window::time();
    while (!window_.shouldClose()) {
        Window::pollEvents();

        const double now = Window::time();
        // A breakpoint or window drag must not teleport the camera.
        const double dt = std::min(now - previous, 0.1);
        previous = now;
        frameDt_ = dt;

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
    double& pixelError = terrain_.lodSettings().maxPixelError;
    if (finerKey_.pressed(down(GLFW_KEY_LEFT_BRACKET))) {
        pixelError = std::max(pixelError / 1.5, 0.5);
    }
    if (coarserKey_.pressed(down(GLFW_KEY_RIGHT_BRACKET))) {
        pixelError = std::min(pixelError * 1.5, 64.0);
    }

    // Right button looks around, unless the press started on the panel.
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

    const double scroll = window_.consumeScroll();
    if (!uiMouse) {
        speedMultiplier_ = std::clamp(speedMultiplier_ * std::pow(1.25, scroll), 0.001, 50.0);
    }

    // Speed scales with altitude: 1 km/s at 1 km, 1000 km/s at 1000 km.
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

double Application::groundHeightAt(const Vec3d& ecef) const {
    // Indexed by direction like the mesh; geocentric vs geodetic vertical is < 0.2 degrees.
    return std::max(elevation_->elevation(normalize(ecef), 1.0), 0.0);
}

void Application::clampCameraToGround() {
    constexpr double kClearance = 2.0;
    Geodetic g = ellipsoid_.ecefToGeodetic(camera_.position());
    const double minimum = groundHeightAt(camera_.position()) + kClearance;
    if (g.height < minimum) {
        g.height = minimum;
        camera_.setPosition(g);
    }
}

void Application::settleTerrain() {
    // Deterministic screenshots: wait until everything is loaded.
    const auto start = std::chrono::steady_clock::now();
    do {
        updateTerrain(std::numeric_limits<std::size_t>::max());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while ((!terrain_.settled() || (tiles_ && !tiles_->idle())) &&
             std::chrono::steady_clock::now() - start < std::chrono::seconds(90));
}

void Application::updateTiles() {
    if (!tiles_) {
        return;
    }
    // Neighbours' feather bands blend towards the tile, so they are rebuilt too.
    constexpr double kDeg = std::numbers::pi / 180.0;
    for (const HgtTileStreamer::Change& c : tiles_->update()) {
        const double lat = (c.lat + 0.5) * kDeg;
        const double lon = (c.lon + 0.5) * kDeg;
        const Vec3d centre{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
        const double halfDiagonal = std::hypot(0.5, 0.5 * std::cos(lat)) * kDeg;
        const double feather = kHighResolutionFeather / 6371000.0;
        terrain_.invalidate(centre, halfDiagonal + 2.0 * feather, c.version,
                            TileStreamingSettings{}.fadeSpacing);
    }
}

void Application::updateTerrain(std::size_t maxUploads) {
    clampCameraToGround();
    updateTiles();
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
        return;
    }
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    gpuTimers_.beginFrame();
    if (drawSettings_.shadows) {
        const auto timer = gpuTimers_.scope(gl::GpuPass::Shadows);
        shadows_.render(camera_, aspect, drawSettings_.sunDirection, terrain_,
                        drawSettings_.geomorph, groundHeightAt(camera_.position()));
    }

    renderTarget_.resize(width, height);
    renderTarget_.bindScene();

    // The shadow pass leaves standard depth state behind.
    glClearColor(0.004f, 0.006f, 0.014f, 1.0f);
    glClearDepth(0.0);
    glDepthFunc(GL_GREATER);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    clouds_.update(camera_, frameDt_);
    atmosphere_.bind();
    {
        const auto timer = gpuTimers_.scope(gl::GpuPass::Sky);
        sky_.draw(camera_, aspect, drawSettings_.sunDirection, drawSettings_.sunIlluminance);
    }
    {
        const auto timer = gpuTimers_.scope(gl::GpuPass::Terrain);
        terrain_.draw(camera_, aspect, drawSettings_, drawSettings_.shadows ? &shadows_ : nullptr);
    }
    if (!drawSettings_.clouds) {
        clouds_.resetHistory();
    } else {
        const auto timer = gpuTimers_.scope(gl::GpuPass::Clouds);
        clouds_.render(camera_, aspect, width, height, drawSettings_.sunDirection,
                       drawSettings_.sunIlluminance, renderTarget_.sceneDepth());
        renderTarget_.bindSceneColor(); // the composite samples the depth
        clouds_.composite(camera_, aspect, width, height, renderTarget_.sceneDepth());
    }

    renderTarget_.bindDisplay();
    {
        const auto timer = gpuTimers_.scope(gl::GpuPass::ToneMap);
        toneMap_.draw(renderTarget_.sceneColor());
    }
    if (uiFrameActive_) {
        const auto timer = gpuTimers_.scope(gl::GpuPass::Ui);
        ui_.render();
        uiFrameActive_ = false;
    }

    renderTarget_.blitToWindow();
}

void Application::printGpuTimes() const {
    std::cout << "GPU ms:";
    for (std::size_t p = 0; p < gl::GpuTimers::kPasses; ++p) {
        const auto pass = static_cast<gl::GpuPass>(p);
        if (gpuTimers_.milliseconds(pass) > 0.0) {
            std::printf(" %s %.2f |", std::string(gl::gpuPassName(pass)).c_str(), gpuTimers_.milliseconds(pass));
        }
    }
    std::printf(" total %.2f\n", gpuTimers_.totalMilliseconds());
    std::fflush(stdout);
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

    // OpenGL rows are bottom-up, PPM top-down.
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
