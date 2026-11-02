#include "app/Application.hpp"

#include "app/gl/DebugOutput.hpp"
#include "core/terrain/CubeSphere.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <future>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace orbis {

namespace {

constexpr int kChunksPerFaceEdge = 8; // 8 x 8 x 6 = 384 chunks
constexpr int kQuadsPerChunkEdge = 32;
constexpr double kDegToRad = std::numbers::pi / 180.0;

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

} // namespace

Application::Application(const AppOptions& options)
    : options_(options), window_(makeWindowConfig(options)), terrain_(kQuadsPerChunkEdge) {
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

    buildPlanet();

    const AppOptions::View& v = options_.view;
    camera_.setPosition({v.latitude * kDegToRad, v.longitude * kDegToRad, v.altitudeKm * 1000.0});
    camera_.setOrientation(v.heading * kDegToRad, v.pitch * kDegToRad);

    // Afternoon sun from the south-west of Central Europe.
    const EnuFrame enu = Ellipsoid::enuFrame(48.7 * kDegToRad, 21.3 * kDegToRad);
    drawSettings_.sunDirection = normalize(enu.up * 0.8 - enu.east * 0.7 - enu.north * 0.4);
}

void Application::buildPlanet() {
    const auto start = std::chrono::steady_clock::now();

    // One task per cube face. The generator is pure (no shared mutable state), so this is safe
    // without locks. Stage 3 replaces it with a real job system and streaming.
    std::vector<std::future<std::vector<ChunkMesh>>> tasks;
    for (const CubeFace face : kAllCubeFaces) {
        tasks.push_back(std::async(std::launch::async, [this, face] {
            std::vector<ChunkMesh> meshes;
            const double size = 2.0 / kChunksPerFaceEdge;
            for (int j = 0; j < kChunksPerFaceEdge; ++j) {
                for (int i = 0; i < kChunksPerFaceEdge; ++i) {
                    const FaceRegion region{face, -1.0 + i * size, -1.0 + j * size, size};
                    meshes.push_back(
                        buildChunkMesh(region, kQuadsPerChunkEdge, ellipsoid_, heightField_));
                }
            }
            return meshes;
        }));
    }

    // GL calls stay on the main thread: an OpenGL context belongs to one thread.
    for (auto& task : tasks) {
        terrain_.upload(task.get());
    }

    const auto ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    std::cout << "Planet: " << terrain_.chunkCount() << " chunks, " << terrain_.triangleCount()
              << " triangles, built in " << static_cast<long long>(ms.count()) << " ms\n";
}

int Application::run() {
    if (options_.screenshotPath) {
        renderFrame();
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

        handleInput(dt);
        renderFrame();
        window_.swapBuffers();
        updateTitle(dt);
    }
    return 0;
}

void Application::handleInput(double dt) {
    GLFWwindow* w = window_.handle();
    const auto down = [w](int key) { return glfwGetKey(w, key) == GLFW_PRESS; };

    if (down(GLFW_KEY_ESCAPE)) {
        window_.requestClose();
    }

    const bool wireframeDown = down(GLFW_KEY_F1);
    if (wireframeDown && !wireframeKeyWasDown_) {
        drawSettings_.wireframe = !drawSettings_.wireframe;
    }
    wireframeKeyWasDown_ = wireframeDown;

    // Mouse look while the right button is held.
    const bool rmb = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
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
    speedMultiplier_ = std::clamp(speedMultiplier_ * std::pow(1.25, window_.consumeScroll()),
                                  0.001, 50.0);

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

void Application::renderFrame() {
    int width = 0;
    int height = 0;
    window_.framebufferSize(width, height);
    if (width == 0 || height == 0) {
        return; // minimised
    }
    renderTarget_.resize(width, height);
    renderTarget_.bind();

    glClearColor(0.004f, 0.006f, 0.014f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    terrain_.draw(camera_, aspect, drawSettings_);

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

    char title[256];
    std::snprintf(title, sizeof(title),
                  "Orbis | %.0f fps | alt %.3f km | %.4f%c %.4f%c | speed x%.3f%s", fps,
                  g.height / 1000.0, std::abs(latDeg), latDeg >= 0 ? 'N' : 'S', std::abs(lonDeg),
                  lonDeg >= 0 ? 'E' : 'W', speedMultiplier_,
                  drawSettings_.wireframe ? " | wireframe" : "");
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
    std::cout << "Screenshot saved to " << path << '\n';
}

} // namespace orbis
