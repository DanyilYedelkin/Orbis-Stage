#include "app/gl/RenderTarget.hpp"

#include <cstddef>
#include <stdexcept>

namespace orbis::gl {

void RenderTarget::resize(int width, int height) {
    if (width == width_ && height == height_ && framebuffer_) {
        return;
    }
    width_ = width;
    height_ = height;

    GLuint id = 0;
    glCreateFramebuffers(1, &id);
    framebuffer_ = Framebuffer{id};
    glCreateRenderbuffers(1, &id);
    color_ = Renderbuffer{id};
    glCreateRenderbuffers(1, &id);
    depth_ = Renderbuffer{id};

    glNamedRenderbufferStorage(color_.get(), GL_RGBA8, width, height);
    glNamedRenderbufferStorage(depth_.get(), GL_DEPTH_COMPONENT32F, width, height);
    glNamedFramebufferRenderbuffer(framebuffer_.get(), GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                   color_.get());
    glNamedFramebufferRenderbuffer(framebuffer_.get(), GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                   depth_.get());

    if (glCheckNamedFramebufferStatus(framebuffer_.get(), GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("RenderTarget: framebuffer is incomplete");
    }
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_.get());
    glViewport(0, 0, width_, height_);
}

void RenderTarget::blitToWindow() const {
    glBlitNamedFramebuffer(framebuffer_.get(), 0, 0, 0, width_, height_, 0, 0, width_, height_,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

std::vector<std::uint8_t> RenderTarget::readRgb() const {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width_) *
                                     static_cast<std::size_t>(height_) * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_.get());
    glPixelStorei(GL_PACK_ALIGNMENT, 1); // rows are not padded to 4 bytes
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

} // namespace orbis::gl
