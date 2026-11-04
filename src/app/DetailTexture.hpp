#pragma once

#include "app/gl/GlHandle.hpp"

#include <cstdint>

namespace orbis {

/// The tiling surface-detail noise (core/terrain/DetailNoise.hpp) on the GPU: RGBA16F with a
/// full mip chain, repeat wrapping, trilinear + anisotropic filtering.
///
/// The mip chain does the anti-aliasing: averaging the stored DERIVATIVES gives exactly the
/// derivative of the averaged height, so distant bumps flatten out correctly instead of
/// sparkling.
class DetailTexture {
public:
    static constexpr int kSize = 512;
    explicit DetailTexture(std::uint64_t seed);

    void bind(unsigned unit) const;
    /// Standard deviation of the stored derivatives (tile units), see DetailNoise.
    float gradientStd() const { return gradientStd_; }

private:
    gl::Texture texture_;
    float gradientStd_ = 1.0f;
};

} // namespace orbis
