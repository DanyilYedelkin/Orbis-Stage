#include "core/atmosphere/AtmosphereModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orbis {

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
                  "const float kEllipsoidAOverB = %.12g;\n"
                  "const float kMetersToAtmosphereKm = %.12g;\n",
                  p.bottomRadius, p.topRadius, p.rayleighScattering.x, p.rayleighScattering.y,
                  p.rayleighScattering.z, p.rayleighScaleHeight, p.mieScattering, p.mieExtinction,
                  p.mieScaleHeight, p.mieAnisotropy, p.ozoneAbsorption.x, p.ozoneAbsorption.y,
                  p.ozoneAbsorption.z, p.ozoneCenter, p.ozoneWidth, p.groundAlbedo.x,
                  p.groundAlbedo.y, p.groundAlbedo.z, kTransmittanceLutWidth,
                  kTransmittanceLutHeight, kMultiScatteringLutSize, kTransmittanceSteps,
                  e.semiMajorAxis() / e.semiMinorAxis(), p.bottomRadius / e.semiMajorAxis());
    return buffer;
}

} // namespace orbis
