#pragma once

#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/math/Vec3.hpp"
#include "core/scene/Camera.hpp"

namespace orbis {

/// Fullscreen sky: blue gradient near the ground fading to black space with altitude, a sun disk,
/// and dusk colours. Uses the same atmosphere functions as the terrain haze, so distant
/// mountains blend into the sky behind them.
class Sky {
public:
    Sky();
    void draw(const Camera& camera, float aspect, const Vec3d& sunDirection) const;

private:
    gl::ShaderProgram shader_;
    gl::VertexArray emptyVao_; ///< Core profile requires a bound VAO even with no attributes.
};

} // namespace orbis
