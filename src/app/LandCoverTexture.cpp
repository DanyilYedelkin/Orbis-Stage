#include "app/LandCoverTexture.hpp"

#include <stdexcept>

namespace orbis {

LandCoverTexture::LandCoverTexture(const LandCover& cover) {
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (cover.width() > maxSize || cover.height() > maxSize) {
        throw std::runtime_error("Land cover map is larger than the GPU's maximum texture size");
    }
    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    texture_ = gl::Texture{id};
    glTextureStorage2D(id, 1, GL_R8UI, cover.width(), cover.height());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // rows of 7200 bytes are not padded
    glTextureSubImage2D(id, 0, 0, 0, cover.width(), cover.height(), GL_RED_INTEGER,
                        GL_UNSIGNED_BYTE, cover.cells().data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // Integer textures must use NEAREST (anything else makes them incomplete).
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void LandCoverTexture::bind(unsigned unit) const { glBindTextureUnit(unit, texture_.get()); }

} // namespace orbis
