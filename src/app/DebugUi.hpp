#pragma once

struct GLFWwindow;

namespace orbis {

/// Dear ImGui context with the GLFW and OpenGL backends.
class DebugUi {
public:
    explicit DebugUi(GLFWwindow* window);
    ~DebugUi();

    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    void beginFrame();
    /// Into the bound framebuffer.
    void render();
    /// For frames that are not drawn (minimised window).
    void discardFrame();

    bool wantsMouse() const;
    bool wantsKeyboard() const;
};

} // namespace orbis
