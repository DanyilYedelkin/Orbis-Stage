#pragma once

#include <string>
#include <string_view>

namespace orbis::gl {

/// Prelude, then `#line 1` so driver errors point into the shader's own file.
inline std::string composeShader(std::string_view common, std::string_view body) {
    std::string source = "#version 450 core\n";
    source += common;
    source += "\n#line 1\n";
    source += body;
    return source;
}

} // namespace orbis::gl
