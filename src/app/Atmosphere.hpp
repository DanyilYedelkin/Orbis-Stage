#pragma once

#include "app/gl/GlHandle.hpp"
#include "core/atmosphere/AtmosphereModel.hpp"
#include "core/geo/Ellipsoid.hpp"

#include <string>

namespace orbis {

/// GPU side of the physically based atmosphere: owns the precomputed lookup tables.
///
/// Both tables depend only on the atmosphere itself (not on the camera or the sun), so they are
/// computed ONCE at startup by compute shaders and then only sampled - the per-frame cost is
/// the raymarch for the sky and the aerial perspective.
class Atmosphere {
public:
    Atmosphere(const AtmosphereParameters& parameters, const Ellipsoid& ellipsoid);

    /// GLSL prelude for every shader that uses the atmosphere: generated constants + functions.
    const std::string& shaderPrelude() const { return prelude_; }
    const AtmosphereParameters& parameters() const { return parameters_; }

    /// Texture unit of the sky irradiance table (0 and 1 are transmittance / multiple
    /// scattering; units in between belong to the passes).
    static constexpr unsigned kIrradianceUnit = 5;

    /// Binds the tables to texture units 0 (transmittance), 1 (multiple scattering) and
    /// kIrradianceUnit (sky irradiance).
    void bind() const;

    /// Reads the transmittance table back from the GPU and compares it with the CPU reference
    /// implementation. Returns the largest absolute difference. Catches shader bugs that would
    /// otherwise only show up as "the sunset looks a bit off".
    double validateAgainstCpu() const;

    /// The irradiance table is validated while it is built: the GPU computes it once with
    /// single scattering only and compares with skyIrradianceSingleScattering() on the CPU.
    /// Largest difference relative to the table's maximum.
    double irradianceValidationError() const { return irradianceError_; }
    /// How much multiple scattering adds to the sky irradiance at noon on the ground (ratio).
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
