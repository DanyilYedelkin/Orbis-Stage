#pragma once

#include "app/gl/GlHandle.hpp"
#include "core/math/Mat4.hpp"
#include "core/math/Vec3.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace orbis::gl {

/// Throws std::runtime_error with the driver's log.
class ShaderProgram {
public:
    ShaderProgram(std::string_view vertexSource, std::string_view fragmentSource,
                  std::string_view debugName);

    static ShaderProgram compute(std::string_view computeSource, std::string_view debugName);

    void use() const { glUseProgram(program_.get()); }

    void set(const char* name, int value) const;
    void set(const char* name, float value) const;
    void set(const char* name, float x, float y) const;
    void set(const char* name, int x, int y) const;
    void set(const char* name, const Vec3f& value) const;
    void set(const char* name, const Mat4f& value) const;

private:
    /// Locations are cached.
    GLint location(const char* name) const;
    ShaderProgram() = default;

    Program program_;
    mutable std::unordered_map<std::string, GLint> locations_;
};

} // namespace orbis::gl
