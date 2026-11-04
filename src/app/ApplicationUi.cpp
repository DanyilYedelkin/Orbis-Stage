// The debug panel. Kept apart from Application.cpp: it is pure presentation of state that lives
// elsewhere, and immediate-mode UI code is long but simple.
#include "app/Application.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace orbis {

namespace {

constexpr double kDeg = std::numbers::pi / 180.0;

struct Place {
    const char* name;
    AppOptions::View view;
    double sunAzimuth;
    double sunElevation;
};

constexpr std::array kEarthPlaces = {
    Place{"Europe from orbit", {48.7164, 21.2611, 14000.0, 0.0, -89.0}, 225.0, 30.0},
    Place{"Alps from 70 km", {44.3, 8.6, 70.0, 5.0, -22.0}, 235.0, 25.0},
    Place{"High Tatras", {48.95, 20.10, 2.6, 8.0, -3.0}, 250.0, 16.0},
    Place{"Himalaya from 60 km", {26.6, 86.2, 60.0, 15.0, -18.0}, 230.0, 30.0},
    Place{"Andes from 50 km", {-33.2, -71.6, 50.0, 80.0, -15.0}, 300.0, 25.0},
};
constexpr std::array kProceduralPlaces = {
    Place{"Planet from orbit", {48.7164, 21.2611, 14000.0, 0.0, -89.0}, 225.0, 30.0},
    Place{"Mountains", {0.30, -140.75, 6.5, 45.0, -10.0}, 225.0, 30.0},
};

/// Small helper: ImGui sliders work in float, our settings are double.
bool sliderDouble(const char* label, double& value, float min, float max, const char* format,
                  ImGuiSliderFlags flags = 0) {
    auto f = static_cast<float>(value);
    if (ImGui::SliderFloat(label, &f, min, max, format, flags)) {
        value = static_cast<double>(f);
        return true;
    }
    return false;
}

} // namespace

void Application::buildDebugPanel(double dt) {
    frameTimes_[frameTimeIndex_] = static_cast<float>(dt * 1000.0);
    frameTimeIndex_ = (frameTimeIndex_ + 1) % frameTimes_.size();

    ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({320.0f, 0.0f}, {420.0f, 2000.0f});
    if (!ImGui::Begin("Orbis", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // ---- Performance
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("%.0f fps  (%.2f ms)", static_cast<double>(io.Framerate),
                1000.0 / static_cast<double>(std::max(io.Framerate, 1.0f)));
    ImGui::PlotLines("##frametime", frameTimes_.data(), static_cast<int>(frameTimes_.size()),
                     static_cast<int>(frameTimeIndex_), "frame time (ms)", 0.0f, 50.0f,
                     {0.0f, 48.0f});

    // ---- Camera
    const CameraBasis b = camera_.basis();
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%.4f %c  %.4f %c", std::abs(b.geodetic.latitude / kDeg),
                    b.geodetic.latitude >= 0 ? 'N' : 'S', std::abs(b.geodetic.longitude / kDeg),
                    b.geodetic.longitude >= 0 ? 'E' : 'W');
        ImGui::Text("altitude %.3f km", b.geodetic.height / 1000.0);
        ImGui::Text("heading %.1f deg  pitch %.1f deg", camera_.heading() / kDeg,
                    camera_.pitch() / kDeg);
        sliderDouble("speed", speedMultiplier_, 0.001f, 50.0f, "x%.3f",
                     ImGuiSliderFlags_Logarithmic);
        float fovDegrees = camera_.verticalFov / static_cast<float>(kDeg);
        if (ImGui::SliderFloat("field of view", &fovDegrees, 15.0f, 90.0f, "%.0f deg")) {
            camera_.verticalFov = fovDegrees * static_cast<float>(kDeg);
        }

        ImGui::TextDisabled("Go to:");
        const bool earth = isEarth_;
        const auto places = [&](const auto& list) {
            for (const Place& place : list) {
                if (ImGui::Button(place.name)) {
                    goTo(place.view, place.sunAzimuth, place.sunElevation);
                }
            }
        };
        if (earth) {
            places(kEarthPlaces);
        } else {
            places(kProceduralPlaces);
        }
    }

    // ---- Terrain statistics
    const TerrainStats& st = terrain_.stats();
    if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("world: %s", isEarth_ ? "Earth (ETOPO1)" : "procedural");
        ImGui::Text("chunks drawn %zu / resident %zu", st.drawnChunks, st.residentChunks);
        ImGui::Text("triangles %.2f M, deepest level %d",
                    static_cast<double>(st.drawnTriangles) / 1e6, st.deepestLevel);
        ImGui::Text("build queue %zu (+%zu running)", st.pendingBuilds, st.runningBuilds);
        ImGui::Text("nodes visited %d, culled %d frustum / %d horizon", st.visited,
                    st.frustumCulled, st.horizonCulled);
        if (st.staleChunks > 0) {
            ImGui::Text("rebuilding %zu chunks for new tile data", st.staleChunks);
        }
        if (tiles_) {
            const HgtTileStreamer::Stats ts = tiles_->stats();
            ImGui::Text("hgt tiles %zu / %zu loaded, %.0f MB", ts.loaded, ts.available,
                        static_cast<double>(ts.bytes) / (1024.0 * 1024.0));
            ImGui::Text("tile loads %llu, evictions %llu, pending %zu",
                        static_cast<unsigned long long>(ts.loads),
                        static_cast<unsigned long long>(ts.evictions), ts.pending);
        }
    }

    // ---- LOD controls
    if (ImGui::CollapsingHeader("Level of detail", ImGuiTreeNodeFlags_DefaultOpen)) {
        LodSettings& lod = terrain_.lodSettings();
        sliderDouble("pixel error", lod.maxPixelError, 0.5f, 32.0f, "%.1f px",
                     ImGuiSliderFlags_Logarithmic);
        ImGui::SliderInt("max level", &lod.maxLevel, 0, 20);
        ImGui::Checkbox("geomorphing", &drawSettings_.geomorph);
        ImGui::SameLine();
        ImGui::Checkbox("LOD colours", &drawSettings_.showLodLevels);
        ImGui::Checkbox("wireframe", &drawSettings_.wireframe);
        ImGui::SameLine();
        ImGui::Checkbox("surface detail", &drawSettings_.surfaceDetail);
        ImGui::SameLine();
        bool frozen = frozenLodCamera_.has_value();
        if (ImGui::Checkbox("freeze LOD", &frozen)) {
            toggleFreeze();
        }
    }

    // ---- Sun
    if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = sliderDouble("azimuth", sunAzimuth_, 0.0f, 360.0f, "%.0f deg");
        changed |= sliderDouble("elevation", sunElevation_, -10.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("intensity", &drawSettings_.sunIlluminance, 1.0f, 20.0f, "%.1f");
        ImGui::Checkbox("shadows", &drawSettings_.shadows);
        ImGui::SameLine();
        ImGui::Checkbox("clouds", &drawSettings_.clouds);
        if (landCover_) {
            ImGui::SameLine();
            ImGui::Checkbox("land cover data", &drawSettings_.landCover);
        }
        if (drawSettings_.clouds) {
            ImGui::SliderFloat("cloud cover", &clouds_.coverage, 0.0f, 1.0f, "%.2f");
        }
        if (changed) {
            // Relative to where the camera is NOW: "sun in the south-west at 20 degrees" means
            // the same thing anywhere on the planet.
            setSun(b.geodetic.latitude, b.geodetic.longitude);
        }
    }

    ImGui::TextDisabled("RMB look, WASD fly, F1-F3 debug, F4 hide panel");
    ImGui::End();
}

} // namespace orbis
