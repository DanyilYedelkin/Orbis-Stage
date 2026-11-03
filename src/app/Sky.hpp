#pragma once

#include "app/Atmosphere.hpp"
#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/math/Vec3.hpp"
#include "core/scene/Camera.hpp"

namespace orbis {

/// Fullscreen physically based sky: every pixel integrates Rayleigh + Mie + ozone scattering
/// along its view ray through the atmosphere (see shaders/atmosphere.glsl). The terrain uses the
/// same integral for aerial perspective, so distant mountains fade into the sky behind them.
class Sky {
public:
    explicit Sky(const Atmosphere& atmosphere);
    void draw(const Camera& camera, float aspect, const Vec3d& sunDirection,
              float sunIlluminance) const;

private:
    gl::ShaderProgram shader_;
    gl::VertexArray emptyVao_; ///< Core profile requires a bound VAO even with no attributes.
};

} // namespace orbis
