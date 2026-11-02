#pragma once

#include "app/TerrainRenderer.hpp"
#include "app/Window.hpp"
#include "app/gl/RenderTarget.hpp"
#include "core/geo/Ellipsoid.hpp"
#include "core/scene/Camera.hpp"
#include "core/terrain/HeightField.hpp"

#include <optional>
#include <string>

namespace orbis {

struct AppOptions {
    int width = 1600;
    int height = 900;
    std::optional<std::string> screenshotPath; ///< Render one frame, save it, exit (for CI).

    /// Initial camera: latitude / longitude / heading / pitch in degrees, altitude in km.
    struct View {
        double latitude = 48.7164; // Kosice
        double longitude = 21.2611;
        double altitudeKm = 14000.0;
        double heading = 0.0;
        double pitch = -89.0;
    } view;
};

class Application {
public:
    explicit Application(const AppOptions& options);
    int run();

private:
    void buildPlanet();
    void handleInput(double dt);
    void renderFrame();
    void updateTitle(double dt);
    void saveScreenshot(const std::string& path) const;

    AppOptions options_;

    // Declaration order = construction order; destruction runs in REVERSE.
    // Window first => the GL context outlives every GL object declared below it.
    Window window_;
    gl::RenderTarget renderTarget_;
    TerrainRenderer terrain_;

    Ellipsoid ellipsoid_ = Ellipsoid::wgs84();
    HeightField heightField_{20261102};
    Camera camera_{ellipsoid_};
    TerrainDrawSettings drawSettings_;

    // Input state
    double speedMultiplier_ = 0.5;
    bool looking_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    bool wireframeKeyWasDown_ = false;

    // Stats
    double titleTimer_ = 0.0;
    int framesSinceTitle_ = 0;
};

} // namespace orbis
