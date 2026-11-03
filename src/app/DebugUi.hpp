#pragma once

struct GLFWwindow;

namespace orbis {

/// Owns the Dear ImGui context and its GLFW + OpenGL backends (RAII).
///
/// Immediate-mode UI: there are no widget objects to keep in sync with the application. Every
/// frame the panel is described from the CURRENT state ("draw a slider showing pixelError"), so
/// it can never show stale values. Ideal for debug tools.
class DebugUi {
public:
    explicit DebugUi(GLFWwindow* window);
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    void beginFrame();
    /// Draws the UI into the currently bound framebuffer. Call after beginFrame().
    void render();
    /// Ends a frame that will not be drawn (e.g. window minimised), keeping ImGui consistent.
    void discardFrame();

    /// True when the mouse / keyboard is over or focused on the UI: the camera must ignore it.
    bool wantsMouse() const;
    bool wantsKeyboard() const;
};

} // namespace orbis
