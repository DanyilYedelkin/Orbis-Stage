#include "app/Window.hpp"

#include "app/gl/DebugOutput.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

namespace orbis {

Window::Window(const WindowConfig& config) {
    glfwSetErrorCallback(
        [](int code, const char* message) { std::cerr << "[GLFW " << code << "] " << message << '\n'; });

    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("glfwInit failed");
    }

    // 4.5 core: needed for glClipControl (reversed-Z) and Direct State Access.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, config.debugContext ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);
    // We render into our own framebuffer, so the window needs no depth buffer.
    glfwWindowHint(GLFW_DEPTH_BITS, 0);

    window_ = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Could not create an OpenGL 4.5 window. Update your GPU driver.");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // v-sync

    if (gladLoadGL(glfwGetProcAddress) == 0) {
        glfwDestroyWindow(window_);
        glfwTerminate();
        throw std::runtime_error("Failed to load OpenGL functions");
    }
    // As early as possible: every GL call after this point - including GPU work done while
    // other members are still being constructed - is checked by the driver in debug builds.
    gl::enableDebugOutput();

    // Lambdas without captures convert to the C function pointers GLFW expects;
    // the user pointer is how they find their Window instance.
    glfwSetWindowUserPointer(window_, this);
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double dy) {
        static_cast<Window*>(glfwGetWindowUserPointer(w))->scroll_ += dy;
    });
}

Window::~Window() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_) == GLFW_TRUE; }
void Window::requestClose() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }
void Window::swapBuffers() { glfwSwapBuffers(window_); }
void Window::pollEvents() { glfwPollEvents(); }
double Window::time() { return glfwGetTime(); }

void Window::framebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(window_, &width, &height);
}

void Window::setTitle(const std::string& title) { glfwSetWindowTitle(window_, title.c_str()); }

void Window::setCursorCaptured(bool captured) {
    glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured && glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE); // no OS mouse acceleration
    }
}

double Window::consumeScroll() {
    const double s = scroll_;
    scroll_ = 0.0;
    return s;
}

} // namespace orbis
