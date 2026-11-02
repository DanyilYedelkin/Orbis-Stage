#pragma once

#include "app/gl/GlHandle.hpp"

#include <cstdint>
#include <vector>

namespace orbis::gl {

/// Off-screen framebuffer: RGBA8 colour + 32-bit FLOAT depth.
///
/// Why not draw straight into the window? The window's default framebuffer usually has a 24-bit
/// fixed-point depth buffer, and reversed-Z only pays off with floating-point depth. So we render
/// here and blit (copy) the colour to the window at the end of the frame.
class RenderTarget {
public:
    /// (Re)allocates storage. Cheap no-op if the size did not change.
    void resize(int width, int height);

    void bind() const;
    void blitToWindow() const;

    /// Bottom-up rows of tightly packed RGB, as glReadPixels returns them.
    std::vector<std::uint8_t> readRgb() const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    Framebuffer framebuffer_;
    Renderbuffer color_;
    Renderbuffer depth_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace orbis::gl
