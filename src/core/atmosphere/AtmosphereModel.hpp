#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Vec3.hpp"

#include <string>

namespace orbis {

/// Hillaire 2020, km and 1/km. The GLSL constants are generated from it.
struct AtmosphereParameters {
    double bottomRadius = 6360.0; ///< Ground (the ellipsoid, mapped to a sphere).
    double topRadius = 6460.0;

    Vec3d rayleighScattering{5.802e-3, 13.558e-3, 33.1e-3};
    double rayleighScaleHeight = 8.0;

    double mieScattering = 3.996e-3;
    double mieExtinction = 4.40e-3;
    double mieScaleHeight = 1.2;
    double mieAnisotropy = 0.8;

    Vec3d ozoneAbsorption{0.650e-3, 1.881e-3, 0.085e-3};
    double ozoneCenter = 25.0;
    double ozoneWidth = 30.0; ///< Tent profile, 0 at center +- width / 2.

    Vec3d groundAlbedo{0.3, 0.3, 0.3};
};

inline constexpr int kTransmittanceLutWidth = 256;
inline constexpr int kTransmittanceLutHeight = 64;
inline constexpr int kMultiScatteringLutSize = 32;
inline constexpr int kTransmittanceSteps = 40; ///< Used identically on CPU and GPU.

/// Sky irradiance table: x = sun cos(zenith) in [-1, 1], y = altitude.
inline constexpr int kIrradianceLutWidth = 64;
inline constexpr int kIrradianceLutHeight = 16;
/// Cosine-weighted stratified directions and steps per ray, identical on CPU and GPU.
inline constexpr int kIrradianceZenithSamples = 16;
inline constexpr int kIrradianceAzimuthSamples = 16;
inline constexpr int kIrradianceSteps = 32;

Vec3d extinctionAt(const AtmosphereParameters& p, double altitude);

/// From radius r, cos(zenith) mu.
double distanceToTop(const AtmosphereParameters& p, double r, double mu);

/// To the top of the atmosphere, midpoint rule.
Vec3d transmittanceToTop(const AtmosphereParameters& p, double r, double mu,
                         int steps = kTransmittanceSteps);

/// Bruneton's transmittance LUT parameterisation, (x_mu, x_r) in [0, 1]^2 <-> (r, mu).
struct RadiusMu {
    double r;
    double mu;
};
RadiusMu transmittanceUnitToRMu(const AtmosphereParameters& p, double xMu, double xR);
void transmittanceRMuToUnit(const AtmosphereParameters& p, double r, double mu, double& xMu,
                            double& xR);

double rayleighPhase(double cosTheta);
double miePhase(const AtmosphereParameters& p, double cosTheta);

/// Twin of sunTransmittance() in atmosphere.glsl: transmittance to the top, softly cut by the
/// planet's shadow over the sun's disk.
Vec3d sunTransmittance(const AtmosphereParameters& p, double r, double muSun);

/// Horizontal surface, sun illuminance 1. Reference for the GPU table.
Vec3d skyIrradianceSingleScattering(const AtmosphereParameters& p, double r, double muSun,
                                    int zenithSamples = kIrradianceZenithSamples,
                                    int azimuthSamples = kIrradianceAzimuthSamples,
                                    int steps = kIrradianceSteps);

/// Texel centres sit on both ends of each axis.
RadiusMu irradianceTexelToRMu(const AtmosphereParameters& p, int x, int y);

std::string toGlsl(const AtmosphereParameters& p, const Ellipsoid& ellipsoid);

} // namespace orbis
