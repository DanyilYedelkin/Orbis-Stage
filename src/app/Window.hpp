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

/// GLFW window with a GL 4.5 core context. Owns glfwInit/glfwTerminate, so only one may exist.
/// GL objects must be destroyed before it.
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

    /// Since the last call.
    double consumeScroll();

private:
    GLFWwindow* window_ = nullptr;
    double scroll_ = 0.0;
};

} // namespace orbis
