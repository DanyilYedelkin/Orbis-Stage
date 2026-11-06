#pragma once

#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"

namespace orbis {

/// HDR scene colour to display.
class ToneMap {
public:
    ToneMap();
    void draw(unsigned sceneColor) const;

private:
    gl::ShaderProgram program_;
    gl::VertexArray emptyVao_;
};

} // namespace orbis
