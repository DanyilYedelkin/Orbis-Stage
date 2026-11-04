#pragma once

#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"

namespace orbis {

/// Final full-screen pass: linear HDR scene colour -> display (ACES filmic curve + gamma).
/// Kept as its own pass so every earlier pass - sky, terrain, clouds - can work in linear light
/// and be composited correctly; tone mapping inside each shader would make blending wrong.
class ToneMap {
public:
    ToneMap();
    /// Reads `sceneColor` (one texel per pixel) into the bound framebuffer.
    void draw(unsigned sceneColor) const;

private:
    gl::ShaderProgram program_;
    gl::VertexArray emptyVao_;
};

} // namespace orbis
