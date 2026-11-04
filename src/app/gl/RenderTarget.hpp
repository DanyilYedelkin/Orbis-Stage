#pragma once

#include "app/gl/GlHandle.hpp"

#include <cstdint>
#include <vector>

namespace orbis::gl {

/// The frame's off-screen targets.
///
///  - SCENE: RGBA16F colour + 32-bit FLOAT depth, both textures. Sky, terrain and clouds write
///    linear HDR radiance here; later passes sample the depth (clouds stop at the terrain) and
///    the colour (tone mapping). A float depth buffer is what makes reversed-Z pay off - the
///    window's default framebuffer usually has a 24-bit fixed-point one.
///  - DISPLAY: RGBA8, what the monitor gets: the tone-mapped image plus the debug panel. It is
///    blitted to the window and read back for screenshots.
class RenderTarget {
public:
    /// (Re)allocates storage. Cheap no-op if the size did not change.
    void resize(int width, int height);

    /// Scene colour + depth (for sky and terrain).
    void bindScene() const;
    /// Scene colour only, no depth attached - for passes that SAMPLE the depth texture (reading
    /// a texture that is also attached to the bound framebuffer would be a feedback loop).
    void bindSceneColor() const;
    void bindDisplay() const;
    void blitToWindow() const;

    GLuint sceneColor() const { return sceneColor_.get(); }
    GLuint sceneDepth() const { return sceneDepth_.get(); }

    /// Bottom-up rows of tightly packed RGB of the display target, as glReadPixels returns them.
    std::vector<std::uint8_t> readRgb() const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    Framebuffer scene_;
    Framebuffer sceneColorOnly_;
    Framebuffer display_;
    Texture sceneColor_;
    Texture sceneDepth_;
    Renderbuffer displayColor_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace orbis::gl
