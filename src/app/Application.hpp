#pragma once

#include "app/Atmosphere.hpp"
#include "app/Clouds.hpp"
#include "app/ToneMap.hpp"
#include "app/DebugUi.hpp"
#include "app/LandCoverTexture.hpp"
#include "app/ShadowCascades.hpp"
#include "app/Sky.hpp"
#include "app/Terrain.hpp"
#include "app/Window.hpp"
#include "app/gl/GpuTimers.hpp"
#include "app/gl/RenderTarget.hpp"
#include "core/geo/Ellipsoid.hpp"
#include "core/scene/Camera.hpp"
#include "core/terrain/ElevationSource.hpp"
#include "core/terrain/HgtTileStreamer.hpp"

#include <array>
#include <cstddef>
#include <memory>

#include <optional>
#include <string>

namespace orbis {

enum class World { Earth, Procedural };

struct AppOptions {
    World world = World::Earth;
    std::optional<std::string> elevationFile;
    std::optional<std::string> hgtDirectory; ///< Default: data/hgt.
    std::size_t tileBudgetMb = 1536;
    std::optional<std::string> landCoverFile;
    bool landCover = true;
    int width = 1600;
    int height = 900;
    std::optional<std::string> screenshotPath; ///< Render, save, exit.
    bool showLodLevels = false;
    bool showUi = true;
    bool geomorph = true;
    bool shadows = true;
    bool surfaceDetail = true;
    bool clouds = true;
    double cloudCoverage = 0.35; ///< 0 clear .. 1 overcast.
    double fovDegrees = 45.0; ///< Vertical.
    bool viewAboveGround = false; ///< --view altitude above the terrain, not sea level.
    int recordFrames = 0;     ///< With screenshotPath: fly forward, save this many frames.
    double recordStep = 0.0;  ///< Metres per recorded frame.
    /// Move only the LOD camera, so frame differences come from LOD transitions alone.
    bool recordLodOnly = false;

    /// Degrees; altitude in km.
    struct View {
        double latitude = 48.7164; // Kosice
        double longitude = 21.2611;
        double altitudeKm = 14000.0;
        double heading = 0.0;
        double pitch = -89.0;
    } view;

    /// Local to the starting point, degrees from north.
    double sunAzimuth = 225.0;
    double sunElevation = 30.0;
};

/// Earth (with streamed .hgt tiles if any) or procedural. `tiles` receives the streamer, if any.
std::unique_ptr<const ElevationSource> makeElevationSource(const AppOptions& options,
                                                           const Ellipsoid& ellipsoid,
                                                           std::shared_ptr<HgtTileStreamer>* tiles = nullptr);

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
    void updateTiles();
    void settleTerrain(); ///< Until the LOD for the current view is complete.
    /// Metres, at the finest LOD's detail.
    double groundHeightAt(const Vec3d& ecef) const;
    void clampCameraToGround();
    void renderFrame();
    void updateTitle(double dt);
    void saveScreenshot(const std::string& path) const;
    void printGpuTimes() const;

    // ApplicationUi.cpp
    void buildDebugPanel(double dt);
    void goTo(const AppOptions::View& view, double sunAzimuth, double sunElevation);
    void setSun(double latitude, double longitude); ///< Radians.
    void toggleFreeze();
    void loadLandCover();

    AppOptions options_;

    // Order matters: the window (GL context) outlives every GL object, and the elevation
    // source outlives the terrain's worker threads.
    Window window_;
    DebugUi ui_;
    Ellipsoid ellipsoid_ = Ellipsoid::wgs84();
    std::shared_ptr<HgtTileStreamer> tiles_;
    std::unique_ptr<const ElevationSource> elevation_;
    bool isEarth_ = false;
    std::optional<LandCoverTexture> landCover_;
    Camera camera_{ellipsoid_};
    gl::RenderTarget renderTarget_;
    gl::GpuTimers gpuTimers_;
    Atmosphere atmosphere_; ///< Before Sky, Clouds and Terrain: their shaders use its prelude.
    Sky sky_;
    Clouds clouds_;
    Terrain terrain_;
    ShadowCascades shadows_;
    ToneMap toneMap_;
    TerrainDrawSettings drawSettings_;

    // When set, LOD selection uses this camera, so culling can be watched from outside.
    std::optional<Camera> frozenLodCamera_;

    // Input state
    double speedMultiplier_ = 0.5;
    bool looking_ = false;
    double lastCursorX_ = 0.0;
    double lastCursorY_ = 0.0;
    double sunAzimuth_ = 225.0;  ///< Degrees, local to where it was last set.
    double sunElevation_ = 30.0;
    bool showUi_ = true;
    bool uiFrameActive_ = false;
    std::array<float, 120> frameTimes_{};
    std::size_t frameTimeIndex_ = 0;
    KeyToggle uiKey_;
    KeyToggle wireframeKey_;
    KeyToggle freezeKey_;
    KeyToggle lodColorKey_;
    KeyToggle finerKey_;
    KeyToggle coarserKey_;

    double frameDt_ = 0.0; ///< 0 in screenshot mode: the clouds hold still.

    // Stats
    double titleTimer_ = 0.0;
    int framesSinceTitle_ = 0;
};

} // namespace orbis
