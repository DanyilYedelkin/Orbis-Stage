#include "app/Sky.hpp"

#include "EmbeddedShaders.hpp"
#include "app/gl/ShaderSource.hpp"

#include <cmath>
#include <string>

namespace orbis {

Sky::Sky(const Atmosphere& atmosphere)
    : shader_(shaders::sky_vert,
              gl::composeShader(std::string(shaders::common_glsl) + atmosphere.shaderPrelude(),
                                shaders::sky_frag),
              "sky"),
      emptyVao_(gl::createVertexArray()) {
    shader_.set("uTransmittanceLut", 0);
    shader_.set("uMultiScatteringLut", 1);
}

void Sky::draw(const Camera& camera, float aspect, const Vec3d& sunDirection,
               float sunIlluminance) const {
    const CameraBasis b = camera.basis();
    const float tanY = std::tan(camera.verticalFov / 2.0f);

    shader_.use();
    shader_.set("uForward", Vec3f(b.forward));
    shader_.set("uRight", Vec3f(b.right));
    shader_.set("uUp", Vec3f(b.up));
    shader_.set("uTanHalfFov", tanY * aspect, tanY);
    shader_.set("uCameraPlanet", Vec3f(camera.position())); // metres; float is fine for km-scale air
    shader_.set("uSunDirection", Vec3f(normalize(sunDirection)));
    shader_.set("uSunIlluminance", sunIlluminance);

    // Background: no depth test, no depth writes - the terrain is drawn over it.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(emptyVao_.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace orbis
