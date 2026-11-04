#pragma once

#include "app/Atmosphere.hpp"
#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/atmosphere/Clouds.hpp"
#include "core/geo/Ellipsoid.hpp"
#include "core/math/Mat4.hpp"
#include "core/scene/Camera.hpp"

#include <array>
#include <string>

namespace orbis {

/// Volumetric clouds. The march runs at half resolution, one sub-pixel of each 2x2 block per
/// frame; resolve reprojects the history (camera and wind) and accumulates; composite upsamples
/// depth-aware over the HDR scene.
class Clouds {
public:
    static constexpr unsigned kWeatherUnit = 6;
    static constexpr unsigned kNoiseUnit = 7;
    static constexpr unsigned kDepthUnit = 8;
    static constexpr int kWeatherFaceSize = 512;
    static constexpr int kNoiseSize = 64;

    Clouds(const CloudParameters& parameters, const Atmosphere& atmosphere, const Ellipsoid& ellipsoid);

    /// Generated constants + clouds.glsl; must follow the atmosphere prelude.
    const std::string& shaderPrelude() const { return prelude_; }

    /// Once per frame, before anything samples the clouds.
    void update(const Camera& camera, double dt);

    /// For programs that sample the density (terrain shadows).
    void bind(const gl::ShaderProgram& program, const Camera& camera, bool enabled,
              bool withDetail = false) const;

    /// Changes the bound framebuffer and viewport.
    void render(const Camera& camera, float aspect, int width, int height, const Vec3d& sunDirection,
                float sunIlluminance, unsigned sceneDepth);
    /// Over the bound framebuffer.
    void composite(const Camera& camera, float aspect, int width, int height,
                   unsigned sceneDepth) const;

    void resetHistory() { historyFrames_ = 0; }

    float coverage = 0.35f;
    float windSpeed = 12.0f;   ///< m/s
    float windHeading = 70.0f; ///< Degrees, direction blown towards.

private:
    struct Target {
        gl::Framebuffer framebuffer;
        gl::Texture color;
        gl::Texture depth;
    };
    void resize(int width, int height);
    void setView(const gl::ShaderProgram& program, const Camera& camera, float aspect, int width,
                 int height) const;

    CloudParameters parameters_;
    Ellipsoid ellipsoid_;
    double bottomRadius_; ///< km
    std::string prelude_;
    gl::Texture weather_;
    gl::Texture noise_;
    gl::ShaderProgram march_;
    gl::ShaderProgram resolve_;
    gl::ShaderProgram composite_;
    gl::VertexArray emptyVao_;

    Target current_;
    std::array<Target, 2> history_;
    int halfWidth_ = 0;
    int halfHeight_ = 0;
    int resolved_ = 0;

    Vec3d windOffset_{}; ///< km, only used modulo a noise tile.
    Vec3d windVelocity_{};
    double frameDt_ = 0.0;
    Mat4f previousViewProjection_{};
    Vec3d previousCamera_{};
    int historyFrames_ = 0;
    unsigned frame_ = 0;
};

} // namespace orbis
