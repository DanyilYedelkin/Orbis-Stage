#pragma once

#include "app/Sky.hpp"
#include "app/Terrain.hpp"
#include "app/Window.hpp"
#include "app/gl/RenderTarget.hpp"
#include "core/geo/Ellipsoid.hpp"
#include "core/scene/Camera.hpp"
#include "core/terrain/ElevationSource.hpp"

#include <memory>

#include <optional>
#include <string>

namespace orbis {

enum class World { Earth, Procedural };

struct AppOptions {
    World world = World::Earth;
    std::optional<std::string> elevationFile; ///< Overrides the bundled Earth data.
    int width = 1600;
    int height = 900;
    std::optional<std::string> screenshotPath; ///< Render one frame, save it, exit (for CI).
    bool showLodLevels = false;                ///< Start with the LOD debug colouring on.

    /// Initial camera: latitude / longitude / heading / pitch in degrees, altitude in km.
    struct View {
        double latitude = 48.7164; // Kosice
        double longitude = 21.2611;
        double altitudeKm = 14000.0;
        double heading = 0.0;
        double pitch = -89.0;
    } view;

    /// Sun as seen from the starting point: azimuth (0 = north, 90 = east) and elevation, degrees.
    double sunAzimuth = 225.0; // south-west: classic "afternoon" light for terrain relief
    double sunElevation = 30.0;
};

/// Edge detector for a key: true only on the frame it goes down.
class KeyToggle {
public:
    bool pressed(bool isDown) {
        const bool edge = isDown && !wasDown_;
        wasDown_ = isDown;
        return edge;
    }

private:
    bool wasDown_ = false;
};

class Application {
public:
    explicit Application(const AppOptions& options);
    int run();

private:
    void handleInput(double dt);
    void updateTerrain(std::size_t maxUploads);
    void renderFrame();
    void updateTitle(double dt);
    void saveScreenshot(const std::string& path) const;

    AppOptions options_;

    // Declaration order = construction order; destruction runs in REVERSE:
    //  - Window first  => the GL context outlives every GL object declared below it.
    //  - Elevation source before Terrain => Terrain's worker threads (which read it) are joined
    //    before the source is destroyed.
    Window window_;
    Ellipsoid ellipsoid_ = Ellipsoid::wgs84();
    std::unique_ptr<const ElevationSource> elevation_;
    Camera camera_{ellipsoid_};
    gl::RenderTarget renderTarget_;
    Sky sky_;
    Terrain terrain_;
    TerrainDrawSettings drawSettings_;

    // Debug: when set, LOD selection uses this camera while you fly around with the real one,
    // so you can watch frustum/horizon culling and refinement from outside.
    std::optional<Camera> frozenLodCamera_;

    // Input state
    double speedMultiplier_ = 0.5;
    bool looking_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    KeyToggle wireframeKey_;
    KeyToggle freezeKey_;
    KeyToggle lodColorKey_;
    KeyToggle finerKey_;
    KeyToggle coarserKey_;

    // Stats
    double titleTimer_ = 0.0;
    int framesSinceTitle_ = 0;
};

} // namespace orbis
