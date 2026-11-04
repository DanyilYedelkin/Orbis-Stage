#pragma once

#include "app/Atmosphere.hpp"
#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/atmosphere/Clouds.hpp"
#include "core/geo/Ellipsoid.hpp"
#include "core/scene/Camera.hpp"

#include <string>

namespace orbis {

/// Volumetric cloud layer: the weather cube map (CPU-generated, core/atmosphere/Clouds), the 3D
/// shape noise (compute shader, once at startup) and the full-screen pass that ray-marches them
/// over the HDR scene. Also feeds the terrain shader what it needs for cloud shadows.
class Clouds {
public:
    static constexpr unsigned kWeatherUnit = 6;
    static constexpr unsigned kNoiseUnit = 7;
    static constexpr unsigned kDepthUnit = 8;
    static constexpr int kWeatherFaceSize = 512;
    static constexpr int kNoiseSize = 64;

    Clouds(const CloudParameters& parameters, const Atmosphere& atmosphere, const Ellipsoid& ellipsoid);

    /// GLSL prelude (generated constants + clouds.glsl) for shaders that sample the clouds;
    /// must follow the atmosphere prelude.
    const std::string& shaderPrelude() const { return prelude_; }

    /// Binds the weather and noise textures and sets the per-frame uniforms of `program`
    /// (which must include the prelude).
    void bind(const gl::ShaderProgram& program, const Camera& camera, bool enabled) const;

    /// Composites the clouds over the scene colour (bound framebuffer: colour only), stopping
    /// rays at `sceneDepth`.
    void draw(const Camera& camera, float aspect, int viewportHeight, const Vec3d& sunDirection,
              float sunIlluminance, unsigned sceneDepth) const;

    float coverage = 0.35f; ///< 0 clear .. 1 overcast; live-tunable.

private:
    CloudParameters parameters_;
    Ellipsoid ellipsoid_;
    double bottomRadius_; ///< Of the atmosphere model, km.
    std::string prelude_;
    gl::Texture weather_;
    gl::Texture noise_;
    gl::ShaderProgram pass_;
    gl::VertexArray emptyVao_;
};

} // namespace orbis
