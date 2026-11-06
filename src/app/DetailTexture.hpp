#pragma once

#include "app/gl/GlHandle.hpp"

#include <cstdint>

namespace orbis {

/// DetailNoise as RGBA16F, mipmapped, anisotropic.
class DetailTexture {
public:
    static constexpr int kSize = 512;
    explicit DetailTexture(std::uint64_t seed);

    void bind(unsigned unit) const;
    float gradientStd() const { return gradientStd_; }

private:
    gl::Texture texture_;
    float gradientStd_ = 1.0f;
};

} // namespace orbis
