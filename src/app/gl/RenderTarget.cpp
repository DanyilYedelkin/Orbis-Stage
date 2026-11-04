#include "app/gl/RenderTarget.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>

namespace orbis::gl {

namespace {

Texture createTexture(GLenum format, int width, int height) {
    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    Texture texture{id};
    glTextureStorage2D(id, 1, format, width, height);
    // Read with texelFetch (one texel per pixel), so no filtering and no mipmaps.
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

Framebuffer createFramebuffer() {
    GLuint id = 0;
    glCreateFramebuffers(1, &id);
    return Framebuffer{id};
}

void checkComplete(const Framebuffer& fb, const char* name) {
    if (glCheckNamedFramebufferStatus(fb.get(), GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("RenderTarget: ") + name + " framebuffer is incomplete");
    }
}

} // namespace

void RenderTarget::resize(int width, int height) {
    if (width == width_ && height == height_ && scene_) {
        return;
    }
    width_ = width;
    height_ = height;

    // RGBA16F: HDR (a sunlit snowfield is ~2, the sun disk far more) with enough precision in the
    // darks; the tone mapper compresses it at the end.
    sceneColor_ = createTexture(GL_RGBA16F, width, height);
    sceneDepth_ = createTexture(GL_DEPTH_COMPONENT32F, width, height);
    scene_ = createFramebuffer();
    glNamedFramebufferTexture(scene_.get(), GL_COLOR_ATTACHMENT0, sceneColor_.get(), 0);
    glNamedFramebufferTexture(scene_.get(), GL_DEPTH_ATTACHMENT, sceneDepth_.get(), 0);
    checkComplete(scene_, "scene");

    sceneColorOnly_ = createFramebuffer();
    glNamedFramebufferTexture(sceneColorOnly_.get(), GL_COLOR_ATTACHMENT0, sceneColor_.get(), 0);
    checkComplete(sceneColorOnly_, "scene colour");

    GLuint id = 0;
    glCreateRenderbuffers(1, &id);
    displayColor_ = Renderbuffer{id};
    glNamedRenderbufferStorage(displayColor_.get(), GL_RGBA8, width, height);
    display_ = createFramebuffer();
    glNamedFramebufferRenderbuffer(display_.get(), GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                   displayColor_.get());
    checkComplete(display_, "display");
}

void RenderTarget::bindScene() const {
    glBindFramebuffer(GL_FRAMEBUFFER, scene_.get());
    glViewport(0, 0, width_, height_);
}

void RenderTarget::bindSceneColor() const {
    glBindFramebuffer(GL_FRAMEBUFFER, sceneColorOnly_.get());
    glViewport(0, 0, width_, height_);
}

void RenderTarget::bindDisplay() const {
    glBindFramebuffer(GL_FRAMEBUFFER, display_.get());
    glViewport(0, 0, width_, height_);
}

void RenderTarget::blitToWindow() const {
    glBlitNamedFramebuffer(display_.get(), 0, 0, 0, width_, height_, 0, 0, width_, height_,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

std::vector<std::uint8_t> RenderTarget::readRgb() const {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width_) *
                                     static_cast<std::size_t>(height_) * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, display_.get());
    glPixelStorei(GL_PACK_ALIGNMENT, 1); // rows are not padded to 4 bytes
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

} // namespace orbis::gl
