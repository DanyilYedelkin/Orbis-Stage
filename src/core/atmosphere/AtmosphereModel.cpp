#include "core/atmosphere/AtmosphereModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace orbis {

namespace {
Vec3d mul(const Vec3d& a, const Vec3d& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
} // namespace

Vec3d extinctionAt(const AtmosphereParameters& p, double altitude) {
    const double rayleigh = std::exp(-altitude / p.rayleighScaleHeight);
    const double mie = std::exp(-altitude / p.mieScaleHeight);
    const double ozone =
        std::max(0.0, 1.0 - std::abs(altitude - p.ozoneCenter) / (p.ozoneWidth * 0.5));
    return p.rayleighScattering * rayleigh + Vec3d{1, 1, 1} * (p.mieExtinction * mie) +
           p.ozoneAbsorption * ozone;
}

double distanceToTop(const AtmosphereParameters& p, double r, double mu) {
    const double discriminant = r * r * (mu * mu - 1.0) + p.topRadius * p.topRadius;
    return std::max(0.0, -r * mu + std::sqrt(std::max(discriminant, 0.0)));
}

Vec3d transmittanceToTop(const AtmosphereParameters& p, double r, double mu, int steps) {
    const double d = distanceToTop(p, r, mu);
    const double dt = d / steps;
    Vec3d opticalDepth{};
    for (int i = 0; i < steps; ++i) {
        const double t = (i + 0.5) * dt;
        const double radius = std::sqrt(r * r + t * t + 2.0 * r * mu * t);
        opticalDepth += extinctionAt(p, radius - p.bottomRadius) * dt;
    }
    return {std::exp(-opticalDepth.x), std::exp(-opticalDepth.y), std::exp(-opticalDepth.z)};
}

RadiusMu transmittanceUnitToRMu(const AtmosphereParameters& p, double xMu, double xR) {
    const double rg2 = p.bottomRadius * p.bottomRadius;
    const double h = std::sqrt(p.topRadius * p.topRadius - rg2);
    const double rho = h * xR;
    const double r = std::sqrt(rho * rho + rg2);
    const double dMin = p.topRadius - r;
    const double dMax = rho + h;
    const double d = dMin + xMu * (dMax - dMin);
    const double mu = d == 0.0 ? 1.0 : (h * h - rho * rho - d * d) / (2.0 * r * d);
    return {r, std::clamp(mu, -1.0, 1.0)};
}

void transmittanceRMuToUnit(const AtmosphereParameters& p, double r, double mu, double& xMu,
                            double& xR) {
    const double rg2 = p.bottomRadius * p.bottomRadius;
    const double h = std::sqrt(p.topRadius * p.topRadius - rg2);
    const double rho = std::sqrt(std::max(r * r - rg2, 0.0));
    const double d = distanceToTop(p, r, mu);
    const double dMin = p.topRadius - r;
    const double dMax = rho + h;
    xMu = (d - dMin) / (dMax - dMin);
    xR = rho / h;
}

double rayleighPhase(double c) { return 3.0 / (16.0 * std::numbers::pi) * (1.0 + c * c); }

double miePhase(const AtmosphereParameters& p, double c) {
    const double g = p.mieAnisotropy;
    const double g2 = g * g;
    const double k = 3.0 / (8.0 * std::numbers::pi) * (1.0 - g2) / (2.0 + g2);
    return k * (1.0 + c * c) / std::pow(1.0 + g2 - 2.0 * g * c, 1.5);
}

Vec3d sunTransmittance(const AtmosphereParameters& p, double r, double muSun) {
    const double sinHorizon = p.bottomRadius / r;
    const double cosHorizon = -std::sqrt(std::max(1.0 - sinHorizon * sinHorizon, 0.0));
    // smoothstep(-0.0047, 0.0047, muSun - cosHorizon), as in the shader.
    const double t = std::clamp((muSun - cosHorizon + 0.0047) / 0.0094, 0.0, 1.0);
    const double visible = t * t * (3.0 - 2.0 * t);
    return visible > 0.0 ? transmittanceToTop(p, r, muSun) * visible : Vec3d{};
}

Vec3d skyIrradianceSingleScattering(const AtmosphereParameters& p, double r, double muSun,
                                    int zenithSamples, int azimuthSamples, int steps) {
    // Local frame: up = +z, the sun in the x-z plane. Cosine-weighted directions make the
    // estimator E = pi * mean(L): the cosine factor is in the sampling density.
    const Vec3d origin{0.0, 0.0, r};
    const Vec3d sun{std::sqrt(std::max(1.0 - muSun * muSun, 0.0)), 0.0, muSun};
    const double pi = std::numbers::pi;
    Vec3d sum{};
    for (int i = 0; i < zenithSamples; ++i) {
        const double u1 = (i + 0.5) / zenithSamples;
        const double sinTheta = std::sqrt(u1);
        const double cosTheta = std::sqrt(1.0 - u1);
        for (int j = 0; j < azimuthSamples; ++j) {
            const double phi = 2.0 * pi * (j + 0.5) / azimuthSamples;
            const Vec3d dir{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
            const double cosView = dot(dir, sun);
            const double phaseR = rayleighPhase(cosView);
            const double phaseM = miePhase(p, cosView);
            const double length = distanceToTop(p, r, cosTheta);

            Vec3d luminance{};
            Vec3d throughput{1.0, 1.0, 1.0};
            double previous = 0.0;
            for (int s = 0; s < steps; ++s) {
                // Quadratic spacing (denser near the surface, where the air is), each step
                // integrated analytically - the same scheme as integrateScattering() on the GPU.
                const double x = (s + 1.0) / steps;
                const double t = length * x * x;
                const double dt = t - previous;
                const Vec3d point = origin + dir * (0.5 * (t + previous));
                previous = t;
                const double radius = std::sqrt(dot(point, point));
                const double altitude = radius - p.bottomRadius;
                const double rayleighDensity = std::exp(-altitude / p.rayleighScaleHeight);
                const double mieDensity = std::exp(-altitude / p.mieScaleHeight);
                const Vec3d extinction = extinctionAt(p, altitude);
                const Vec3d scattering =
                    mul(p.rayleighScattering * (rayleighDensity * phaseR) +
                            Vec3d{1, 1, 1} * (p.mieScattering * mieDensity * phaseM),
                        sunTransmittance(p, radius, dot(point, sun) / radius));
                const Vec3d step{std::exp(-extinction.x * dt), std::exp(-extinction.y * dt),
                                 std::exp(-extinction.z * dt)};
                const Vec3d integral{scattering.x * (1.0 - step.x) / std::max(extinction.x, 1e-9),
                                     scattering.y * (1.0 - step.y) / std::max(extinction.y, 1e-9),
                                     scattering.z * (1.0 - step.z) / std::max(extinction.z, 1e-9)};
                luminance += mul(throughput, integral);
                throughput = mul(throughput, step);
            }
            sum += luminance;
        }
    }
    return sum * (pi / (zenithSamples * azimuthSamples));
}

RadiusMu irradianceTexelToRMu(const AtmosphereParameters& p, int x, int y) {
    const double muSun = -1.0 + 2.0 * x / (kIrradianceLutWidth - 1.0);
    // Keep the bottom row a hair above the ground and the top row below the top, like the
    // multiple-scattering table: exactly on the top boundary a ray has zero length.
    const double r = p.bottomRadius + 0.001 +
                     (p.topRadius - p.bottomRadius - 0.002) * y / (kIrradianceLutHeight - 1.0);
    return {r, muSun};
}

std::string toGlsl(const AtmosphereParameters& p, const Ellipsoid& e) {
    char buffer[2048];
    std::snprintf(buffer, sizeof(buffer),
                  "// Generated from AtmosphereParameters (src/core/atmosphere). Do not edit.\n"
                  "const float kBottomRadius = %.9g;\n"
                  "const float kTopRadius = %.9g;\n"
                  "const vec3 kRayleighScattering = vec3(%.9g, %.9g, %.9g);\n"
                  "const float kRayleighScaleHeight = %.9g;\n"
                  "const float kMieScattering = %.9g;\n"
                  "const float kMieExtinction = %.9g;\n"
                  "const float kMieScaleHeight = %.9g;\n"
                  "const float kMieAnisotropy = %.9g;\n"
                  "const vec3 kOzoneAbsorption = vec3(%.9g, %.9g, %.9g);\n"
                  "const float kOzoneCenter = %.9g;\n"
                  "const float kOzoneWidth = %.9g;\n"
                  "const vec3 kGroundAlbedo = vec3(%.9g, %.9g, %.9g);\n"
                  "const vec2 kTransmittanceSize = vec2(%d.0, %d.0);\n"
                  "const float kMultiScatteringSize = %d.0;\n"
                  "const int kTransmittanceSteps = %d;\n"
                  "const vec2 kIrradianceSize = vec2(%d.0, %d.0);\n"
                  "const int kIrradianceZenithSamples = %d;\n"
                  "const int kIrradianceAzimuthSamples = %d;\n"
                  "const int kIrradianceSteps = %d;\n"
                  "const float kEllipsoidAOverB = %.12g;\n"
                  "const float kMetersToAtmosphereKm = %.12g;\n",
                  p.bottomRadius, p.topRadius, p.rayleighScattering.x, p.rayleighScattering.y,
                  p.rayleighScattering.z, p.rayleighScaleHeight, p.mieScattering, p.mieExtinction,
                  p.mieScaleHeight, p.mieAnisotropy, p.ozoneAbsorption.x, p.ozoneAbsorption.y,
                  p.ozoneAbsorption.z, p.ozoneCenter, p.ozoneWidth, p.groundAlbedo.x,
                  p.groundAlbedo.y, p.groundAlbedo.z, kTransmittanceLutWidth,
                  kTransmittanceLutHeight, kMultiScatteringLutSize, kTransmittanceSteps,
                  kIrradianceLutWidth, kIrradianceLutHeight, kIrradianceZenithSamples,
                  kIrradianceAzimuthSamples, kIrradianceSteps,
                  e.semiMajorAxis() / e.semiMinorAxis(), p.bottomRadius / e.semiMajorAxis());
    return buffer;
}

} // namespace orbis
