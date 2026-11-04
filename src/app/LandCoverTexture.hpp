#pragma once

#include "app/gl/GlHandle.hpp"
#include "core/terrain/LandCover.hpp"

namespace orbis {

/// The land cover map on the GPU: one R8UI texel per cell, read with texelFetch.
///
/// Classes are categories, so the texture is never filtered by the hardware (the average of
/// "forest" and "desert" is not a class). The terrain shader fetches the 4 cells around each
/// fragment itself, blends their MATERIALS, and warps the lookup with noise so the 5.6 km cells
/// turn into irregular natural boundaries instead of a visible grid.
class LandCoverTexture {
public:
    explicit LandCoverTexture(const LandCover& cover);

    void bind(unsigned unit) const;

private:
    gl::Texture texture_;
};

} // namespace orbis
