#include "app/Clouds.hpp"

#include "EmbeddedShaders.hpp"
#include "app/gl/ShaderSource.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

namespace orbis {

namespace {
constexpr std::uint64_t kWeatherSeed = 20261104;

constexpr double kResetDistance = 20000.0; // metres per frame: a teleport, not a flight

double positiveModulo(double x, double period) { return x - std::floor(x / period) * period; }

Vec3f moduloVec(const Vec3d& v, double period) {
    return {static_cast<float>(positiveModulo(v.x, period)), static_cast<float>(positiveModulo(v.y, period)),
            static_cast<float>(positiveModulo(v.z, period))};
}

std::string passSource(const Atmosphere& atmosphere, const std::string& clouds, std::string_view body) {
    return gl::composeShader(std::string(shaders::common_glsl) + atmosphere.shaderPrelude() + clouds +
                                 std::string(shaders::cloud_view_glsl),
                             body);
}

void setSamplers(const gl::ShaderProgram& program) {
    program.set("uTransmittanceLut", 0);
    program.set("uMultiScatteringLut", 1);
    program.set("uIrradianceLut", static_cast<int>(Atmosphere::kIrradianceUnit));
}
} // namespace

Clouds::Clouds(const CloudParameters& parameters, const Atmosphere& atmosphere,
               const Ellipsoid& ellipsoid)
    : parameters_(parameters), ellipsoid_(ellipsoid),
      bottomRadius_(atmosphere.parameters().bottomRadius),
      prelude_(toGlsl(parameters) + std::string(shaders::clouds_glsl)),
      march_(shaders::sky_vert, passSource(atmosphere, prelude_, shaders::clouds_march_frag), "cloud march"),
      resolve_(shaders::sky_vert, passSource(atmosphere, prelude_, shaders::clouds_resolve_frag),
               "cloud resolve"),
      composite_(shaders::sky_vert, passSource(atmosphere, prelude_, shaders::clouds_composite_frag),
                 "cloud composite"),
      emptyVao_(gl::createVertexArray()) {
    coverage = static_cast<float>(parameters.coverage);
    const auto start = std::chrono::steady_clock::now();

    // A cube map: equally sized texels in every direction, no poles.
    {
        const std::vector<float> cells = makeCloudWeather(kWeatherFaceSize, kWeatherSeed);
        GLuint id = 0;
        glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &id);
        weather_ = gl::Texture{id};
        glTextureStorage2D(id, 1, GL_R16F, kWeatherFaceSize, kWeatherFaceSize);
        // With DSA a cube map is uploaded as 6 layers, in GL face order.
        glTextureSubImage3D(id, 0, 0, 0, 0, kWeatherFaceSize, kWeatherFaceSize, 6, GL_RED, GL_FLOAT,
                            cells.data());
        glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    }

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

    setSamplers(march_);
    march_.set("uSceneDepth", static_cast<int>(kDepthUnit));
    resolve_.set("uCurrent", 9);
    resolve_.set("uCurrentDepth", 10);
    resolve_.set("uHistory", 11);
    resolve_.set("uHistoryDepth", 12);
    composite_.set("uResolved", 9);
    composite_.set("uResolvedDepth", 10);
    composite_.set("uSceneDepth", static_cast<int>(kDepthUnit));

    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    std::cout << "Clouds: weather " << kWeatherFaceSize << "^2 x 6, shape noise " << kNoiseSize
              << "^3 (" << static_cast<long long>(ms.count()) << " ms)\n";
}

void Clouds::resize(int width, int height) {
    const int w = (width + 1) / 2;
    const int h = (height + 1) / 2;
    if (w == halfWidth_ && h == halfHeight_) {
        return;
    }
    halfWidth_ = w;
    halfHeight_ = h;
    const auto make = [w, h](Target& t) {
        const auto texture = [w, h](GLenum format) {
            GLuint id = 0;
            glCreateTextures(GL_TEXTURE_2D, 1, &id);
            glTextureStorage2D(id, 1, format, w, h);
            glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return gl::Texture{id};
        };
        t.color = texture(GL_RGBA16F);
        t.depth = texture(GL_RG32F);
        GLuint fbo = 0;
        glCreateFramebuffers(1, &fbo);
        t.framebuffer = gl::Framebuffer{fbo};
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, t.color.get(), 0);
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT1, t.depth.get(), 0);
        const GLenum buffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glNamedFramebufferDrawBuffers(fbo, 2, buffers);
    };
    make(current_);
    make(history_[0]);
    make(history_[1]);
    historyFrames_ = 0;
}

void Clouds::update(const Camera& camera, double dt) {
    const EnuFrame enu = camera.basis().enu;
    const double heading = static_cast<double>(windHeading) * std::numbers::pi / 180.0;
    windVelocity_ = (enu.north * std::cos(heading) + enu.east * std::sin(heading)) *
                    static_cast<double>(windSpeed);
    // Atmosphere-model km, like toAtmosphere(): z scaled by a/b, metres scaled to km.
    const double scale = bottomRadius_ / ellipsoid_.semiMajorAxis();
    const double aOverB = ellipsoid_.semiMajorAxis() / ellipsoid_.semiMinorAxis();
    windOffset_ += Vec3d{windVelocity_.x, windVelocity_.y, windVelocity_.z * aOverB} * (scale * dt);
    frameDt_ = dt;
}

void Clouds::bind(const gl::ShaderProgram& program, const Camera& camera, bool enabled,
                  bool withDetail) const {
    program.set("uCloudWeather", static_cast<int>(kWeatherUnit));
    program.set("uCloudNoise", static_cast<int>(kNoiseUnit));
    program.set("uCloudsEnabled", enabled && coverage > 0.0f ? 1 : 0);
    program.set("uCloudCoverage", coverage);
    // Noise coordinates are eye-relative + this origin, which is reduced modulo the noise period
    // in double: world-anchored (and wind-advected) coordinates that stay small floats.
    const Vec3d& c = camera.position();
    const double scale = bottomRadius_ / ellipsoid_.semiMajorAxis();
    const double aOverB = ellipsoid_.semiMajorAxis() / ellipsoid_.semiMinorAxis();
    const Vec3d eye{c.x * scale, c.y * scale, c.z * aOverB * scale};
    program.set("uCloudOrigin", moduloVec(eye - windOffset_, parameters_.regionalTile));
    if (withDetail) {
        program.set("uCloudDetailShift", moduloVec(windOffset_ * -0.5, parameters_.detailTile));
    }
    glBindTextureUnit(kWeatherUnit, weather_.get());
    glBindTextureUnit(kNoiseUnit, noise_.get());
}

void Clouds::setView(const gl::ShaderProgram& program, const Camera& camera, float aspect, int width,
                     int height) const {
    const CameraBasis b = camera.basis();
    const float tanY = std::tan(camera.verticalFov / 2.0f);
    program.set("uForward", Vec3f(b.forward));
    program.set("uRight", Vec3f(b.right));
    program.set("uUp", Vec3f(b.up));
    program.set("uTanHalfFov", tanY * aspect, tanY);
    program.set("uFullSize", static_cast<float>(width), static_cast<float>(height));
}

void Clouds::render(const Camera& camera, float aspect, int width, int height,
                    const Vec3d& sunDirection, float sunIlluminance, unsigned sceneDepth) {
    resize(width, height);
    ++frame_;
    if (historyFrames_ > 0 && distance(camera.position(), previousCamera_) > kResetDistance) {
        historyFrames_ = 0;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glBindVertexArray(emptyVao_.get());
    glViewport(0, 0, halfWidth_, halfHeight_);

    // Visit the 4 pixels of each 2x2 block in a rotated order, so consecutive frames are spread.
    constexpr int kOrder[4][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
    const int* sub = kOrder[frame_ % 4];
    const float tanY = std::tan(camera.verticalFov / 2.0f);

    march_.use();
    setView(march_, camera, aspect, width, height);
    march_.set("uNearPlane", camera.nearPlane);
    march_.set("uCameraPlanet", Vec3f(camera.position()));
    march_.set("uSunDirection", Vec3f(normalize(sunDirection)));
    march_.set("uSunIlluminance", sunIlluminance);
    march_.set("uPixelAngle", 2.0f * tanY / static_cast<float>(std::max(height, 1)));
    march_.set("uSubPixel", sub[0], sub[1]);
    march_.set("uFrame", static_cast<float>(frame_ % 1024));
    bind(march_, camera, true, true);
    glBindTextureUnit(kDepthUnit, sceneDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, current_.framebuffer.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);

    const int previous = resolved_;
    const int next = 1 - resolved_;
    resolve_.use();
    setView(resolve_, camera, aspect, width, height);
    resolve_.set("uCameraPlanet", Vec3f(camera.position()));
    // An exact running average until 10 frames are in, then an exponential one.
    const float weight = std::min(static_cast<float>(historyFrames_) / static_cast<float>(historyFrames_ + 1), 0.9f);
    resolve_.set("uHistoryWeight", weight);
    resolve_.set("uPreviousViewProjection", previousViewProjection_);
    resolve_.set("uCameraDelta", Vec3f(camera.position() - previousCamera_));
    resolve_.set("uWindDelta", Vec3f(windVelocity_ * frameDt_));
    glBindTextureUnit(9, current_.color.get());
    glBindTextureUnit(10, current_.depth.get());
    glBindTextureUnit(11, history_[static_cast<std::size_t>(previous)].color.get());
    glBindTextureUnit(12, history_[static_cast<std::size_t>(previous)].depth.get());
    glBindFramebuffer(GL_FRAMEBUFFER, history_[static_cast<std::size_t>(next)].framebuffer.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);

    resolved_ = next;
    historyFrames_ = std::min(historyFrames_ + 1, 1000);
    previousViewProjection_ = camera.projectionMatrix(aspect) * camera.viewRotationMatrix();
    previousCamera_ = camera.position();

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Clouds::composite(const Camera& camera, float aspect, int width, int height,
                       unsigned sceneDepth) const {
    composite_.use();
    setView(composite_, camera, aspect, width, height);
    composite_.set("uNearPlane", camera.nearPlane);
    const Target& resolved = history_[static_cast<std::size_t>(resolved_)];
    glBindTextureUnit(9, resolved.color.get());
    glBindTextureUnit(10, resolved.depth.get());
    glBindTextureUnit(kDepthUnit, sceneDepth);

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_SRC_ALPHA); // scene * transmittance + cloud light
    glBindVertexArray(emptyVao_.get());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace orbis
