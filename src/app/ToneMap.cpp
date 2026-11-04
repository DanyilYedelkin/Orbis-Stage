#include "app/ToneMap.hpp"

#include "EmbeddedShaders.hpp"
#include "app/gl/ShaderSource.hpp"

namespace orbis {

ToneMap::ToneMap()
    : program_(shaders::sky_vert, gl::composeShader(shaders::common_glsl, shaders::tonemap_frag),
               "tonemap"),
      emptyVao_(gl::createVertexArray()) {
    program_.set("uSceneColor", 0);
}

void ToneMap::draw(unsigned sceneColor) const {
    program_.use();
    glBindTextureUnit(0, sceneColor);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(emptyVao_.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace orbis
