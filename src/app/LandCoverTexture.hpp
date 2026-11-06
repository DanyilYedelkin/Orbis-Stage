#pragma once

#include "app/gl/GlHandle.hpp"
#include "core/terrain/LandCover.hpp"

namespace orbis {

/// R8UI, unfiltered: the shader blends the materials of the four surrounding classes itself.
class LandCoverTexture {
public:
    explicit LandCoverTexture(const LandCover& cover);

    void bind(unsigned unit) const;

private:
    gl::Texture texture_;
};

} // namespace orbis
