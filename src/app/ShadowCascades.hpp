#pragma once

#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/math/Mat4.hpp"
#include "core/math/Vec3.hpp"
#include "core/scene/Camera.hpp"

#include <array>

namespace orbis {

class Terrain;

/// Cascaded shadow maps for the sun.
///
/// One shadow map cannot cover both the rocks at your feet and a mountain 30 km away, so the
/// view is split into distance ranges, each with its own map (same resolution, growing area).
/// Everything is camera-relative like the main pass; only the snapping to the texel grid is done
/// in world (ECEF) double precision - that is what stops the shadow edges from crawling when the
/// camera moves.
class ShadowCascades {
public:
    static constexpr int kCascades = 3;
    static constexpr int kResolution = 2048;

    ShadowCascades();

    /// Fits the cascades to the camera and renders the terrain's depth from the sun into them.
    /// `groundHeight`: terrain height under the camera (metres), to size the cascades by the
    /// camera's height above the ground rather than above sea level.
    void render(const Camera& camera, float aspect, const Vec3d& sunDirection,
                const Terrain& terrain, bool geomorph, double groundHeight);

    /// Binds the shadow map array to texture unit 2.
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
