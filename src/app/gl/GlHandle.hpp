#pragma once

#include <glad/gl.h>

#include <utility>

namespace orbis::gl {

/// Move-only owner of a GL object name.
template <typename Deleter>
class GlHandle {
public:
    GlHandle() = default;
    explicit GlHandle(GLuint id) : id_(id) {}
    ~GlHandle() { reset(); }

    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;

    GlHandle(GlHandle&& other) noexcept : id_(std::exchange(other.id_, 0)) {}
    GlHandle& operator=(GlHandle&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

    GLuint get() const { return id_; }
    explicit operator bool() const { return id_ != 0; }

    void reset() {
        if (id_ != 0) {
            Deleter{}(id_);
            id_ = 0;
        }
    }

private:
    GLuint id_ = 0;
};

struct BufferDeleter {
    void operator()(GLuint id) const { glDeleteBuffers(1, &id); }
};
struct VertexArrayDeleter {
    void operator()(GLuint id) const { glDeleteVertexArrays(1, &id); }
};
struct FramebufferDeleter {
    void operator()(GLuint id) const { glDeleteFramebuffers(1, &id); }
};
struct RenderbufferDeleter {
    void operator()(GLuint id) const { glDeleteRenderbuffers(1, &id); }
};
struct TextureDeleter {
    void operator()(GLuint id) const { glDeleteTextures(1, &id); }
};
struct ProgramDeleter {
    void operator()(GLuint id) const { glDeleteProgram(id); }
};
struct ShaderDeleter {
    void operator()(GLuint id) const { glDeleteShader(id); }
};

using Buffer = GlHandle<BufferDeleter>;
using VertexArray = GlHandle<VertexArrayDeleter>;
using Framebuffer = GlHandle<FramebufferDeleter>;
using Renderbuffer = GlHandle<RenderbufferDeleter>;
using Program = GlHandle<ProgramDeleter>;
using Texture = GlHandle<TextureDeleter>;
using Shader = GlHandle<ShaderDeleter>;

inline Buffer createBuffer() {
    GLuint id = 0;
    glCreateBuffers(1, &id);
    return Buffer{id};
}
inline VertexArray createVertexArray() {
    GLuint id = 0;
    glCreateVertexArrays(1, &id);
    return VertexArray{id};
}

} // namespace orbis::gl
