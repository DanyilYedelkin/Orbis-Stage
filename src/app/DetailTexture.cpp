#include "app/DetailTexture.hpp"

#include "core/terrain/DetailNoise.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace orbis {

namespace {
// GL 4.6 / EXT_texture_filter_anisotropic (same values); our GL 4.5 loader does not define them.
constexpr GLenum kTextureMaxAnisotropy = 0x84FE;
constexpr GLenum kMaxTextureMaxAnisotropy = 0x84FF;

bool hasExtension(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext != nullptr && std::strcmp(ext, name) == 0) {
            return true;
        }
    }
    return false;
}
} // namespace

DetailTexture::DetailTexture(std::uint64_t seed) {
    const DetailNoise noise = makeDetailNoise(kSize, seed);
    gradientStd_ = static_cast<float>(noise.gradientStd);
    const auto levels = static_cast<GLsizei>(std::bit_width(static_cast<unsigned>(kSize)));

    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    texture_ = gl::Texture{id};
    // Half floats: the derivatives reach a few hundred tile units, beyond 8-bit normalised range.
    glTextureStorage2D(id, levels, GL_RGBA16F, kSize, kSize);
    glTextureSubImage2D(id, 0, 0, 0, kSize, kSize, GL_RGBA, GL_FLOAT, noise.texels.data());
    glGenerateTextureMipmap(id);
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // Ground seen at a grazing angle would blur to its mip average otherwise. Anisotropic
    // filtering is core in GL 4.6 and a near-universal extension before that.
    if (hasExtension("GL_EXT_texture_filter_anisotropic") ||
        hasExtension("GL_ARB_texture_filter_anisotropic")) {
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(kMaxTextureMaxAnisotropy, &maxAnisotropy);
        glTextureParameterf(id, kTextureMaxAnisotropy, std::min(maxAnisotropy, 8.0f));
    }
}

void DetailTexture::bind(unsigned unit) const { glBindTextureUnit(unit, texture_.get()); }

} // namespace orbis
