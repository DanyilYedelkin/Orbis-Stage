#pragma once

#include "app/gl/GlHandle.hpp"
#include "core/math/Mat4.hpp"
#include "core/math/Vec3.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace orbis::gl {

/// Compiled + linked GLSL program. Throws std::runtime_error with the driver's log on failure,
/// so a typo in a shader is a readable message, not a black screen.
class ShaderProgram {
public:
    ShaderProgram(std::string_view vertexSource, std::string_view fragmentSource,
                  std::string_view debugName);

    void use() const { glUseProgram(program_.get()); }

    void set(const char* name, int value) const;
    void set(const char* name, float value) const;
    void set(const char* name, const Vec3f& value) const;
    void set(const char* name, const Mat4f& value) const;

private:
    /// Looks the location up once, then caches it (glGetUniformLocation is a string search).
    GLint location(const char* name) const;

    Program program_;
    mutable std::unordered_map<std::string, GLint> locations_;
};

} // namespace orbis::gl
