#pragma once

#include "app/gl/GlHandle.hpp"

#include <cstdint>
#include <vector>

namespace orbis::gl {

/// Scene: RGBA16F + D32F textures (reversed Z needs float depth, which the default framebuffer
/// rarely has). Display: RGBA8 after tone mapping and UI, blitted to the window.
class RenderTarget {
public:
    void resize(int width, int height);

    void bindScene() const;
    /// Without depth, for passes that sample it.
    void bindSceneColor() const;
    void bindDisplay() const;
    void blitToWindow() const;

    GLuint sceneColor() const { return sceneColor_.get(); }
    GLuint sceneDepth() const { return sceneDepth_.get(); }

    /// Display target, bottom-up.
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
