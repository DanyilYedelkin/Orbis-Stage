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

/// Sky irradiance table: x = sun cosine-of-zenith in [-1, 1], y = altitude, linear.
inline constexpr int kIrradianceLutWidth = 64;
inline constexpr int kIrradianceLutHeight = 16;
/// Integration of the sky over the hemisphere: cosine-weighted stratified directions
/// (zenith x azimuth strata) and steps per view ray. Used identically on CPU and GPU.
inline constexpr int kIrradianceZenithSamples = 16;
inline constexpr int kIrradianceAzimuthSamples = 16;
inline constexpr int kIrradianceSteps = 32;

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

/// Phase functions: Rayleigh, and Cornette-Shanks for Mie (Henyey-Greenstein with a better
/// backward lobe). Both integrate to 1 over the sphere.
double rayleighPhase(double cosTheta);
double miePhase(const AtmosphereParameters& p, double cosTheta);

/// Sunlight (for a sun of illuminance 1) reaching radius r with the sun at cosine-of-zenith
/// muSun: transmittance to the top of the atmosphere, softly cut by the planet's shadow over
/// the sun's ~0.5 degree disk. The CPU twin of sunTransmittance() in atmosphere.glsl.
Vec3d sunTransmittance(const AtmosphereParameters& p, double r, double muSun);

/// Irradiance on a HORIZONTAL surface at radius r from the sky alone (no direct sun), for a sun
/// of illuminance 1, from SINGLE scattering: E = integral over the upper hemisphere of L cos.
/// The reference the GPU table is validated against (the GPU adds multiple scattering on top,
/// which needs its own table and has no cheap CPU twin).
Vec3d skyIrradianceSingleScattering(const AtmosphereParameters& p, double r, double muSun,
                                    int zenithSamples = kIrradianceZenithSamples,
                                    int azimuthSamples = kIrradianceAzimuthSamples,
                                    int steps = kIrradianceSteps);

/// Irradiance table texel (x, y) <-> (r, muSun). Texel centres sit on both ends of each axis.
RadiusMu irradianceTexelToRMu(const AtmosphereParameters& p, int x, int y);

/// GLSL source with every parameter as a constant, plus the mapping from planet-centred metres
/// on `ellipsoid` to the spherical atmosphere in km (scale z by a/b, then by bottomRadius/a).
std::string toGlsl(const AtmosphereParameters& p, const Ellipsoid& ellipsoid);

} // namespace orbis
