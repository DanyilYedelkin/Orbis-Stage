#include "core/atmosphere/AtmosphereModel.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>

using namespace orbis;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
const AtmosphereParameters kEarth{};
}

TEST_CASE("LUT parameterisation round-trips", "[atmosphere]") {
    for (double xR = 0.0; xR <= 1.0; xR += 0.125) {
        for (double xMu = 0.0; xMu <= 1.0; xMu += 0.0625) {
            const RadiusMu rm = transmittanceUnitToRMu(kEarth, xMu, xR);
            double xMu2 = 0.0;
            double xR2 = 0.0;
            transmittanceRMuToUnit(kEarth, rm.r, rm.mu, xMu2, xR2);
            REQUIRE_THAT(xR2, WithinAbs(xR, 1e-9));
            REQUIRE_THAT(xMu2, WithinAbs(xMu, 1e-6));
        }
    }
}

TEST_CASE("zenith transmittance matches the analytic optical depth", "[atmosphere]") {
    // Straight up from the ground, each component integrates in closed form:
    //   Rayleigh: beta * H * (1 - e^(-100/H)),  Mie: beta * H_M,  ozone tent: beta * width / 2.
    const double top = kEarth.topRadius - kEarth.bottomRadius;
    const auto expected = [&](double rayleigh, double ozone) {
        const double tau = rayleigh * kEarth.rayleighScaleHeight *
                               (1.0 - std::exp(-top / kEarth.rayleighScaleHeight)) +
                           kEarth.mieExtinction * kEarth.mieScaleHeight +
                           ozone * kEarth.ozoneWidth / 2.0;
        return std::exp(-tau);
    };
    const Vec3d t = transmittanceToTop(kEarth, kEarth.bottomRadius, 1.0, 4000);
    CHECK_THAT(t.x, WithinRel(expected(kEarth.rayleighScattering.x, kEarth.ozoneAbsorption.x), 1e-4));
    CHECK_THAT(t.y, WithinRel(expected(kEarth.rayleighScattering.y, kEarth.ozoneAbsorption.y), 1e-4));
    CHECK_THAT(t.z, WithinRel(expected(kEarth.rayleighScattering.z, kEarth.ozoneAbsorption.z), 1e-4));
    // Blue is scattered most: that is why the sky is blue and the noon sun barely yellow.
    CHECK(t.x > t.y);
    CHECK(t.y > t.z);
}

TEST_CASE("the low sun is red: blue is removed along the long horizontal path", "[atmosphere]") {
    const Vec3d zenith = transmittanceToTop(kEarth, kEarth.bottomRadius, 1.0, 2000);
    const Vec3d horizon = transmittanceToTop(kEarth, kEarth.bottomRadius + 0.01, 0.0, 2000);
    CHECK(horizon.z < 0.01);                                // almost no blue left
    CHECK(horizon.x / horizon.z > 10.0 * zenith.x / zenith.z); // far redder than at noon
}

TEST_CASE("the GPU step count is accurate enough", "[atmosphere]") {
    // The shaders integrate with kTransmittanceSteps samples; compare with a fine reference
    // over the whole LUT domain.
    double worst = 0.0;
    for (double xR = 0.0; xR <= 1.0; xR += 0.1) {
        for (double xMu = 0.0; xMu <= 1.0; xMu += 0.05) {
            const RadiusMu rm = transmittanceUnitToRMu(kEarth, xMu, xR);
            const Vec3d coarse = transmittanceToTop(kEarth, rm.r, rm.mu);
            const Vec3d fine = transmittanceToTop(kEarth, rm.r, rm.mu, 4000);
            worst = std::max(worst, std::abs(coarse.z - fine.z));
        }
    }
    CHECK(worst < 5e-3);
}

TEST_CASE("generated GLSL contains every parameter", "[atmosphere]") {
    const std::string glsl = toGlsl(kEarth, Ellipsoid::wgs84());
    for (const char* name : {"kBottomRadius", "kTopRadius", "kRayleighScattering", "kMieAnisotropy",
                             "kOzoneAbsorption", "kGroundAlbedo", "kTransmittanceSize",
                             "kEllipsoidAOverB", "kMetersToAtmosphereKm"}) {
        CHECK(glsl.find(name) != std::string::npos);
    }
}
