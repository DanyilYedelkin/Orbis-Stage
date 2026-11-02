#include "app/gl/DebugOutput.hpp"

#include <glad/gl.h>

#include <iostream>

namespace orbis::gl {

namespace {

void GLAD_API_PTR onDebugMessage([[maybe_unused]] GLenum source, GLenum type,
                                 [[maybe_unused]] GLuint id, GLenum severity,
                                 [[maybe_unused]] GLsizei length, const GLchar* message,
                                 [[maybe_unused]] const void* userParam) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return; // "buffer will use VIDEO memory" style chatter
    }
    const char* kind = type == GL_DEBUG_TYPE_ERROR ? "ERROR" : "warning";
    std::cerr << "[GL " << kind << "] " << message << '\n';
}

} // namespace

void enableDebugOutput() {
    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if ((flags & GL_CONTEXT_FLAG_DEBUG_BIT) == 0) {
        return;
    }
    glEnable(GL_DEBUG_OUTPUT);
    // Synchronous: the callback runs inside the failing GL call, so a breakpoint there shows
    // the exact line that caused the error.
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(onDebugMessage, nullptr);
}

} // namespace orbis::gl
