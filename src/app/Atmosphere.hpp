#pragma once

#include "app/gl/GlHandle.hpp"
#include "core/atmosphere/AtmosphereModel.hpp"
#include "core/geo/Ellipsoid.hpp"

#include <string>

namespace orbis {

/// Atmosphere LUTs, computed once at startup by compute shaders.
class Atmosphere {
public:
    Atmosphere(const AtmosphereParameters& parameters, const Ellipsoid& ellipsoid);

    const std::string& shaderPrelude() const { return prelude_; }
    const AtmosphereParameters& parameters() const { return parameters_; }

    /// Transmittance and multiple scattering are bound to units 0 and 1.
    static constexpr unsigned kIrradianceUnit = 5;

    void bind() const;

    /// Max absolute difference of the transmittance LUT from the CPU reference.
    double validateAgainstCpu() const;

    /// Single-scattering pass against skyIrradianceSingleScattering(), relative to the maximum.
    double irradianceValidationError() const { return irradianceError_; }
    /// Ground irradiance at noon, multiple over single scattering.
    double multipleScatteringGain() const { return multipleScatteringGain_; }

private:
    void computeIrradiance(const gl::Texture& target, bool multipleScattering) const;

    AtmosphereParameters parameters_;
    std::string prelude_;
    gl::Texture transmittance_;
    gl::Texture multiScattering_;
    gl::Texture irradiance_;
    double irradianceError_ = 0.0;
    double multipleScatteringGain_ = 1.0;
};

} // namespace orbis
