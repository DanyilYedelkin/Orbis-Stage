#include "app/gl/ShaderProgram.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace orbis::gl {

namespace {

Shader compile(GLenum stage, std::string_view source, std::string_view debugName) {
    Shader shader{glCreateShader(stage)};
    const GLchar* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader.get(), 1, &text, &length);
    glCompileShader(shader.get());

    GLint ok = GL_FALSE;
    glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader.get(), GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> log(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetShaderInfoLog(shader.get(), logLength, nullptr, log.data());
        const char* suffix = stage == GL_VERTEX_SHADER     ? ".vert'"
                             : stage == GL_FRAGMENT_SHADER ? ".frag'"
                                                           : ".comp'";
        throw std::runtime_error(std::string("Shader compile error in '") +
                                 std::string(debugName) + suffix + ":\n" + log.data());
    }
    return shader;
}

} // namespace

ShaderProgram::ShaderProgram(std::string_view vertexSource, std::string_view fragmentSource,
                             std::string_view debugName)
    : program_(glCreateProgram()) {
    const Shader vs = compile(GL_VERTEX_SHADER, vertexSource, debugName);
    const Shader fs = compile(GL_FRAGMENT_SHADER, fragmentSource, debugName);

    glAttachShader(program_.get(), vs.get());
    glAttachShader(program_.get(), fs.get());
    glLinkProgram(program_.get());
    // The program keeps the compiled code; the shader objects are no longer needed.
    glDetachShader(program_.get(), vs.get());
    glDetachShader(program_.get(), fs.get());

    GLint ok = GL_FALSE;
    glGetProgramiv(program_.get(), GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program_.get(), GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> log(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetProgramInfoLog(program_.get(), logLength, nullptr, log.data());
        throw std::runtime_error("Shader link error in '" + std::string(debugName) + "':\n" +
                                 log.data());
    }
}

ShaderProgram ShaderProgram::compute(std::string_view computeSource, std::string_view debugName) {
    ShaderProgram result;
    result.program_ = Program{glCreateProgram()};
    const Shader cs = compile(GL_COMPUTE_SHADER, computeSource, debugName);
    glAttachShader(result.program_.get(), cs.get());
    glLinkProgram(result.program_.get());
    glDetachShader(result.program_.get(), cs.get());

    GLint ok = GL_FALSE;
    glGetProgramiv(result.program_.get(), GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(result.program_.get(), GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> log(static_cast<std::size_t>(std::max(logLength, 1)));
        glGetProgramInfoLog(result.program_.get(), logLength, nullptr, log.data());
        throw std::runtime_error("Compute link error in '" + std::string(debugName) + "':\n" +
                                 log.data());
    }
    return result;
}

GLint ShaderProgram::location(const char* name) const {
    const auto it = locations_.find(name);
    if (it != locations_.end()) {
        return it->second;
    }
    const GLint loc = glGetUniformLocation(program_.get(), name);
    if (loc < 0) {
        // Not fatal (the compiler removes unused uniforms), but usually a typo. Warn once.
        std::cerr << "[shader] uniform '" << name << "' not found or unused\n";
    }
    locations_.emplace(name, loc);
    return loc;
}

// glProgramUniform* (GL 4.1+) writes to a program without glUseProgram first.
void ShaderProgram::set(const char* name, int value) const {
    glProgramUniform1i(program_.get(), location(name), value);
}
void ShaderProgram::set(const char* name, float value) const {
    glProgramUniform1f(program_.get(), location(name), value);
}
void ShaderProgram::set(const char* name, float x, float y) const {
    glProgramUniform2f(program_.get(), location(name), x, y);
}
void ShaderProgram::set(const char* name, const Vec3f& value) const {
    glProgramUniform3f(program_.get(), location(name), value.x, value.y, value.z);
}
void ShaderProgram::set(const char* name, const Mat4f& value) const {
    glProgramUniformMatrix4fv(program_.get(), location(name), 1, GL_FALSE, value.data());
}

} // namespace orbis::gl
