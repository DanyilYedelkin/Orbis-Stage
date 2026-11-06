#pragma once

#include "app/Atmosphere.hpp"
#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/math/Vec3.hpp"
#include "core/scene/Camera.hpp"

namespace orbis {

class Sky {
public:
    explicit Sky(const Atmosphere& atmosphere);
    void draw(const Camera& camera, float aspect, const Vec3d& sunDirection,
              float sunIlluminance) const;

private:
    gl::ShaderProgram shader_;
    gl::VertexArray emptyVao_; ///< Core profile needs one bound.
};

} // namespace orbis
