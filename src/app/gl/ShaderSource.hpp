#pragma once

#include <string>
#include <string_view>

namespace orbis::gl {

/// Assembles a fragment/vertex shader from shared GLSL + the shader's own code.
/// `#line 1` after the shared part keeps the driver's error messages pointing at the right line
/// of the shader file you are actually editing.
inline std::string composeShader(std::string_view common, std::string_view body) {
    std::string source = "#version 450 core\n";
    source += common;
    source += "\n#line 1\n";
    source += body;
    return source;
}

} // namespace orbis::gl
