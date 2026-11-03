#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Vec3.hpp"

#include <string>

namespace orbis {

/// Physical description of an Earth-like atmosphere, after S. Hillaire, "A Scalable and
/// Production Ready Sky and Atmosphere Rendering Technique" (EGSR 2020). Units: km and 1/km.
///
/// This struct is the SINGLE source of truth: the GLSL constants used by the GPU are generated
/// from it (toGlsl), and the CPU reference functions below use it too, so the unit tests and the
/// shaders cannot silently disagree.
struct AtmosphereParameters {
    double bottomRadius = 6360.0; ///< Ground (the ellipsoid, mapped to a sphere).
    double topRadius = 6460.0;    ///< 100 km of atmosphere.

    Vec3d rayleighScattering{5.802e-3, 13.558e-3, 33.1e-3}; ///< Air molecules: scatters blue.
    double rayleighScaleHeight = 8.0;

    double mieScattering = 3.996e-3; ///< Aerosols: grey haze and the glow around the sun.
    double mieExtinction = 4.40e-3;
    double mieScaleHeight = 1.2;
    double mieAnisotropy = 0.8; ///< Strongly forward scattering.

    Vec3d ozoneAbsorption{0.650e-3, 1.881e-3, 0.085e-3}; ///< Absorbs orange: twilight turns blue.
    double ozoneCenter = 25.0;
    double ozoneWidth = 30.0; ///< Tent profile, 0 at center +- width / 2.

    Vec3d groundAlbedo{0.3, 0.3, 0.3};
};

inline constexpr int kTransmittanceLutWidth = 256;
inline constexpr int kTransmittanceLutHeight = 64;
inline constexpr int kMultiScatteringLutSize = 32;
inline constexpr int kTransmittanceSteps = 40; ///< Used identically on CPU and GPU.

/// Extinction coefficient (1/km) at an altitude above the ground (km).
Vec3d extinctionAt(const AtmosphereParameters& p, double altitude);

/// Distance from radius r along a ray with cosine-of-zenith mu to the top of the atmosphere.
double distanceToTop(const AtmosphereParameters& p, double r, double mu);

/// Transmittance from (r, mu) to the top of the atmosphere, midpoint rule with `steps` samples.
Vec3d transmittanceToTop(const AtmosphereParameters& p, double r, double mu,
                         int steps = kTransmittanceSteps);

/// Transmittance LUT parameterisation (E. Bruneton, "Precomputed Atmospheric Scattering").
/// Unit coordinates (x_mu, x_r) in [0, 1]^2 <-> (r, mu). Spends texels where transmittance
/// changes fast (near the horizon) instead of spreading them uniformly over mu.
struct RadiusMu {
    double r;
    double mu;
};
RadiusMu transmittanceUnitToRMu(const AtmosphereParameters& p, double xMu, double xR);
void transmittanceRMuToUnit(const AtmosphereParameters& p, double r, double mu, double& xMu,
                            double& xR);

/// GLSL source with every parameter as a constant, plus the mapping from planet-centred metres
/// on `ellipsoid` to the spherical atmosphere in km (scale z by a/b, then by bottomRadius/a).
std::string toGlsl(const AtmosphereParameters& p, const Ellipsoid& ellipsoid);

} // namespace orbis
