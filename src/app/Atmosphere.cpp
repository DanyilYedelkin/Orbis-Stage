#include "app/Atmosphere.hpp"

#include "EmbeddedShaders.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "app/gl/ShaderSource.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace orbis {

namespace {

gl::Texture createLut(int width, int height) {
    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    gl::Texture texture{id};
    // RGBA16F: half floats are plenty for transmittance / scattering, and image stores to
    // 3-component formats are not allowed.
    glTextureStorage2D(id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

GLuint groups(int size) { return static_cast<GLuint>((size + 7) / 8); }

std::vector<float> readRgba(const gl::Texture& texture, int width, int height) {
    std::vector<float> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glGetTextureImage(texture.get(), 0, GL_RGBA, GL_FLOAT,
                      static_cast<GLsizei>(pixels.size() * sizeof(float)), pixels.data());
    return pixels;
}

} // namespace

Atmosphere::Atmosphere(const AtmosphereParameters& parameters, const Ellipsoid& ellipsoid)
    : parameters_(parameters),
      prelude_(toGlsl(parameters, ellipsoid) + std::string(shaders::atmosphere_glsl)),
      transmittance_(createLut(kTransmittanceLutWidth, kTransmittanceLutHeight)),
      multiScattering_(createLut(kMultiScatteringLutSize, kMultiScatteringLutSize)),
      irradiance_(createLut(kIrradianceLutWidth, kIrradianceLutHeight)) {
    {
        const auto program = gl::ShaderProgram::compute(
            gl::composeShader(prelude_, shaders::transmittance_comp), "transmittance");
        program.use();
        glBindImageTexture(0, transmittance_.get(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute(groups(kTransmittanceLutWidth), groups(kTransmittanceLutHeight), 1);
    }
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);

    {
        const auto program = gl::ShaderProgram::compute(
            gl::composeShader(prelude_, shaders::multiscattering_comp), "multiscattering");
        program.use();
        program.set("uTransmittanceLut", 0);
        glBindTextureUnit(0, transmittance_.get());
        glBindImageTexture(0, multiScattering_.get(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute(groups(kMultiScatteringLutSize), groups(kMultiScatteringLutSize), 1);
    }
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);

    // Single scattering first, checked against the CPU reference, then the real table.
    {
        const gl::Texture single = createLut(kIrradianceLutWidth, kIrradianceLutHeight);
        computeIrradiance(single, false);
        computeIrradiance(irradiance_, true);
        const std::vector<float> gpu = readRgba(single, kIrradianceLutWidth, kIrradianceLutHeight);
        const std::vector<float> full = readRgba(irradiance_, kIrradianceLutWidth, kIrradianceLutHeight);

        float maximum = 0.0f;
        for (const float v : gpu) {
            maximum = std::max(maximum, v);
        }
        double worst = 0.0;
        // A sparse grid of texels (the CPU reference is slow), always including both ends.
        for (int y = 0; y < kIrradianceLutHeight; y += 5) {
            for (int x = 0; x < kIrradianceLutWidth; x += 9) {
                const RadiusMu rm = irradianceTexelToRMu(parameters_, x, y);
                const Vec3d cpu = skyIrradianceSingleScattering(parameters_, rm.r, rm.mu);
                const std::size_t i = (static_cast<std::size_t>(y) * kIrradianceLutWidth +
                                       static_cast<std::size_t>(x)) * 4;
                worst = std::max({worst, std::abs(cpu.x - static_cast<double>(gpu[i])),
                                  std::abs(cpu.y - static_cast<double>(gpu[i + 1])),
                                  std::abs(cpu.z - static_cast<double>(gpu[i + 2]))});
            }
        }
        irradianceError_ = worst / std::max(static_cast<double>(maximum), 1e-12);
        const std::size_t noon = static_cast<std::size_t>(kIrradianceLutWidth - 1) * 4 + 1;
        multipleScatteringGain_ = static_cast<double>(full[noon]) / std::max(static_cast<double>(gpu[noon]), 1e-12);
    }
    glUseProgram(0);
}

void Atmosphere::computeIrradiance(const gl::Texture& target, bool multipleScattering) const {
    const auto program = gl::ShaderProgram::compute(
        gl::composeShader(prelude_, shaders::irradiance_comp), "irradiance");
    program.use();
    program.set("uTransmittanceLut", 0);
    program.set("uMultiScatteringLut", 1);
    program.set("uMultipleScattering", multipleScattering ? 1 : 0);
    glBindTextureUnit(0, transmittance_.get());
    glBindTextureUnit(1, multiScattering_.get());
    glBindImageTexture(0, target.get(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(groups(kIrradianceLutWidth), groups(kIrradianceLutHeight), 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
}

void Atmosphere::bind() const {
    glBindTextureUnit(0, transmittance_.get());
    glBindTextureUnit(1, multiScattering_.get());
    glBindTextureUnit(kIrradianceUnit, irradiance_.get());
}

double Atmosphere::validateAgainstCpu() const {
    std::vector<float> gpu(static_cast<std::size_t>(kTransmittanceLutWidth) *
                           kTransmittanceLutHeight * 4);
    glGetTextureImage(transmittance_.get(), 0, GL_RGBA, GL_FLOAT,
                      static_cast<GLsizei>(gpu.size() * sizeof(float)), gpu.data());

    double worst = 0.0;
    for (int y = 0; y < kTransmittanceLutHeight; y += 3) {
        for (int x = 0; x < kTransmittanceLutWidth; x += 5) {
            // Same texel -> (r, mu) mapping as the compute shader.
            const RadiusMu rm = transmittanceUnitToRMu(
                parameters_, x / double(kTransmittanceLutWidth - 1), y / double(kTransmittanceLutHeight - 1));
            const Vec3d cpu = transmittanceToTop(parameters_, rm.r, rm.mu);
            const std::size_t i = (static_cast<std::size_t>(y) * kTransmittanceLutWidth +
                                   static_cast<std::size_t>(x)) * 4;
            worst = std::max({worst, std::abs(cpu.x - static_cast<double>(gpu[i])),
                              std::abs(cpu.y - static_cast<double>(gpu[i + 1])),
                              std::abs(cpu.z - static_cast<double>(gpu[i + 2]))});
        }
    }
    return worst;
}

} // namespace orbis
