#pragma once

#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/math/Mat4.hpp"
#include "core/math/Vec3.hpp"
#include "core/scene/Camera.hpp"

#include <array>

namespace orbis {

class Terrain;

/// Sun shadow cascades. Camera-relative like the main pass, but snapped to the texel grid in
/// ECEF doubles so the edges do not crawl as the camera moves.
class ShadowCascades {
public:
    static constexpr int kCascades = 3;
    static constexpr int kResolution = 2048;

    ShadowCascades();

    /// `groundHeight` under the camera sizes the cascades by height above ground.
    void render(const Camera& camera, float aspect, const Vec3d& sunDirection,
                const Terrain& terrain, bool geomorph, double groundHeight);

    /// Texture unit 2.
    void bind() const;

    const std::array<Mat4f, kCascades>& lightMatrices() const { return lightMatrices_; }
    const std::array<float, kCascades>& splits() const { return splits_; }
    const std::array<float, kCascades>& texelSizes() const { return texelSizes_; }

private:
    gl::Texture depth_;
    std::array<gl::Framebuffer, kCascades> framebuffers_;
    gl::ShaderProgram program_;
    std::array<Mat4f, kCascades> lightMatrices_{};
    std::array<float, kCascades> splits_{};
    std::array<float, kCascades> texelSizes_{};
};

} // namespace orbis
