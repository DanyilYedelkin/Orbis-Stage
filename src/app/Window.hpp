#pragma once

#include <string>

struct GLFWwindow;

namespace orbis {

struct WindowConfig {
    int width = 1600;
    int height = 900;
    std::string title = "Orbis";
    bool visible = true;
    bool debugContext = false;
};

/// GLFW window + OpenGL 4.5 core context (RAII). Owns GLFW's global init/terminate, so exactly
/// one Window may exist - enough for this app and it keeps the lifetime rules obvious.
///
/// IMPORTANT: every GL object must be destroyed BEFORE the Window (the context dies with it).
/// Application declares the Window as its first member, so C++ destroys it last.
class Window {
public:
    explicit Window(const WindowConfig& config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    GLFWwindow* handle() const { return window_; }

    bool shouldClose() const;
    void requestClose();
    void swapBuffers();
    static void pollEvents();
    static double time();

    void framebufferSize(int& width, int& height) const;
    void setTitle(const std::string& title);
    void setCursorCaptured(bool captured);

    /// Mouse-wheel movement accumulated since the last call.
    double consumeScroll();

private:
    GLFWwindow* window_ = nullptr;
    double scroll_ = 0.0;
};

} // namespace orbis
