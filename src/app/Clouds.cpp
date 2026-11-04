#include "app/Clouds.hpp"

#include "EmbeddedShaders.hpp"
#include "app/gl/ShaderSource.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace orbis {

namespace {
constexpr std::uint64_t kWeatherSeed = 20261104;

double positiveModulo(double x, double period) { return x - std::floor(x / period) * period; }
} // namespace

Clouds::Clouds(const CloudParameters& parameters, const Atmosphere& atmosphere,
               const Ellipsoid& ellipsoid)
    : parameters_(parameters), ellipsoid_(ellipsoid),
      bottomRadius_(atmosphere.parameters().bottomRadius),
      prelude_(toGlsl(parameters) + std::string(shaders::clouds_glsl)),
      pass_(shaders::sky_vert,
            gl::composeShader(std::string(shaders::common_glsl) + atmosphere.shaderPrelude() + prelude_,
                              shaders::clouds_frag),
            "clouds"),
      emptyVao_(gl::createVertexArray()) {
    coverage = static_cast<float>(parameters.coverage);
    const auto start = std::chrono::steady_clock::now();

    // Weather: a cube map, so every direction gets equally sized texels and there are no poles.
    {
        const std::vector<float> cells = makeCloudWeather(kWeatherFaceSize, kWeatherSeed);
        GLuint id = 0;
        glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &id);
        weather_ = gl::Texture{id};
        glTextureStorage2D(id, 1, GL_R16F, kWeatherFaceSize, kWeatherFaceSize);
        // DSA uploads cube faces as layers of a 3D image: z = face in GL order (+X, -X, ...).
        glTextureSubImage3D(id, 0, 0, 0, 0, kWeatherFaceSize, kWeatherFaceSize, 6, GL_RED, GL_FLOAT,
                            cells.data());
        glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); // filter across face edges
    }

    // Shape noise: 3D, tiling, generated on the GPU (a few million Worley evaluations).
    {
        GLuint id = 0;
        glCreateTextures(GL_TEXTURE_3D, 1, &id);
        noise_ = gl::Texture{id};
        const auto levels = static_cast<GLsizei>(std::log2(kNoiseSize)) + 1;
        glTextureStorage3D(id, levels, GL_RGBA8, kNoiseSize, kNoiseSize, kNoiseSize);
        const auto program = gl::ShaderProgram::compute(
            gl::composeShader("", shaders::cloud_noise_comp), "cloud noise");
        program.use();
        program.set("uSize", kNoiseSize);
        glBindImageTexture(0, id, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
        const auto groups = static_cast<GLuint>((kNoiseSize + 3) / 4);
        glDispatchCompute(groups, groups, groups);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
        glGenerateTextureMipmap(id);
        glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        for (const GLenum wrap : {GLenum{GL_TEXTURE_WRAP_S}, GLenum{GL_TEXTURE_WRAP_T}, GLenum{GL_TEXTURE_WRAP_R}}) {
            glTextureParameteri(id, wrap, GL_REPEAT);
        }
        glUseProgram(0);
    }

    pass_.set("uTransmittanceLut", 0);
    pass_.set("uMultiScatteringLut", 1);
    pass_.set("uIrradianceLut", static_cast<int>(Atmosphere::kIrradianceUnit));
    pass_.set("uSceneDepth", static_cast<int>(kDepthUnit));

    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    std::cout << "Clouds: weather " << kWeatherFaceSize << "^2 x 6, shape noise " << kNoiseSize
              << "^3 (" << static_cast<long long>(ms.count()) << " ms)\n";
}

void Clouds::bind(const gl::ShaderProgram& program, const Camera& camera, bool enabled) const {
    program.set("uCloudWeather", static_cast<int>(kWeatherUnit));
    program.set("uCloudNoise", static_cast<int>(kNoiseUnit));
    program.set("uCloudsEnabled", enabled && coverage > 0.0f ? 1 : 0);
    program.set("uCloudCoverage", coverage);
    // The eye in the atmosphere model's km (as toAtmosphere() does on the GPU), in DOUBLE, modulo
    // the noise period: world-anchored noise coordinates that stay small floats.
    const Vec3d& c = camera.position();
    const double scale = bottomRadius_ / ellipsoid_.semiMajorAxis(); // = kMetersToAtmosphereKm
    const double aOverB = ellipsoid_.semiMajorAxis() / ellipsoid_.semiMinorAxis();
    const double tile = parameters_.regionalTile; // a multiple of every noise tile
    program.set("uCloudOrigin", Vec3f{static_cast<float>(positiveModulo(c.x * scale, tile)),
                                      static_cast<float>(positiveModulo(c.y * scale, tile)),
                                      static_cast<float>(positiveModulo(c.z * aOverB * scale, tile))});
    glBindTextureUnit(kWeatherUnit, weather_.get());
    glBindTextureUnit(kNoiseUnit, noise_.get());
}

void Clouds::draw(const Camera& camera, float aspect, int viewportHeight,
                  const Vec3d& sunDirection, float sunIlluminance, unsigned sceneDepth) const {
    const CameraBasis b = camera.basis();
    const float tanY = std::tan(camera.verticalFov / 2.0f);
    pass_.use();
    pass_.set("uForward", Vec3f(b.forward));
    pass_.set("uRight", Vec3f(b.right));
    pass_.set("uUp", Vec3f(b.up));
    pass_.set("uTanHalfFov", tanY * aspect, tanY);
    pass_.set("uCameraPlanet", Vec3f(camera.position()));
    pass_.set("uSunDirection", Vec3f(normalize(sunDirection)));
    pass_.set("uSunIlluminance", sunIlluminance);
    pass_.set("uNearPlane", camera.nearPlane);
    pass_.set("uPixelAngle", 2.0f * tanY / static_cast<float>(std::max(viewportHeight, 1)));
    bind(pass_, camera, true);
    glBindTextureUnit(kDepthUnit, sceneDepth);

    // result = cloud light + scene * cloud transmittance (alpha).
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_SRC_ALPHA);
    glBindVertexArray(emptyVao_.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace orbis
