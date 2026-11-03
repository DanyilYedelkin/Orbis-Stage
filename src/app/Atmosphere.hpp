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

    /// Binds the tables to texture units 0 (transmittance) and 1 (multiple scattering).
    void bind() const;

    /// Reads the transmittance table back from the GPU and compares it with the CPU reference
    /// implementation. Returns the largest absolute difference. Catches shader bugs that would
    /// otherwise only show up as "the sunset looks a bit off".
    double validateAgainstCpu() const;

private:
    AtmosphereParameters parameters_;
    std::string prelude_;
    gl::Texture transmittance_;
    gl::Texture multiScattering_;
};

} // namespace orbis
