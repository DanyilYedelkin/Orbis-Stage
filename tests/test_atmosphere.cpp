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

// ---------------------------------------------------------------------------- sky irradiance

TEST_CASE("phase functions integrate to one over the sphere", "[atmosphere][irradiance]") {
    const int n = 20000;
    double rayleigh = 0.0;
    double mie = 0.0;
    for (int i = 0; i < n; ++i) {
        const double c = -1.0 + 2.0 * (i + 0.5) / n; // integral over the sphere = 2 pi * int dc
        rayleigh += rayleighPhase(c);
        mie += miePhase(kEarth, c);
    }
    CHECK_THAT(rayleigh * 2.0 * 3.14159265358979 * 2.0 / n, WithinAbs(1.0, 1e-6));
    CHECK_THAT(mie * 2.0 * 3.14159265358979 * 2.0 / n, WithinAbs(1.0, 2e-3));
}

TEST_CASE("clear-sky irradiance is blue, a modest share of the sun, and fades through dusk",
          "[atmosphere][irradiance]") {
    const double ground = kEarth.bottomRadius + 0.001;
    const Vec3d noon = skyIrradianceSingleScattering(kEarth, ground, 1.0);
    const Vec3d direct = sunTransmittance(kEarth, ground, 1.0);

    // Blue sky: Rayleigh scattering grows with frequency.
    CHECK(noon.z > noon.y);
    CHECK(noon.y > noon.x);
    // Diffuse / direct on a horizontal surface under a high sun: a few to ~25 % for a clean
    // atmosphere (measured clear-sky diffuse fractions are ~10-15 % in green). Single scattering
    // alone sits at the low end; the GPU table adds the multiple scattering.
    const double fraction = noon.y / direct.y;
    CHECK(fraction > 0.03);
    CHECK(fraction < 0.25);

    // Decreasing as the sun sets, still light in civil twilight, near darkness 12 deg below.
    double previous = noon.y;
    for (double mu = 0.9; mu >= -0.05; mu -= 0.1) {
        const double e = skyIrradianceSingleScattering(kEarth, ground, mu).y;
        CHECK(e < previous);
        previous = e;
    }
    const double twilight = skyIrradianceSingleScattering(kEarth, ground, std::sin(-3.0 * 3.14159265358979 / 180.0)).y;
    const double night = skyIrradianceSingleScattering(kEarth, ground, std::sin(-12.0 * 3.14159265358979 / 180.0)).y;
    CHECK(twilight > 1e-3 * noon.y);
    CHECK(night < 1e-3 * noon.y);
}

TEST_CASE("the irradiance integration is converged at the GPU sample counts",
          "[atmosphere][irradiance]") {
    // Rays near the horizon carry the most light (long paths through dense air) and converge
    // slowest; 16 zenith strata were needed for ~2 % (8 gave 3.5 % too little).
    for (const double mu : {1.0, 0.2}) {
        for (const double altitude : {0.001, 10.0}) {
            const double r = kEarth.bottomRadius + altitude;
            const Vec3d coarse = skyIrradianceSingleScattering(kEarth, r, mu);
            const Vec3d fine = skyIrradianceSingleScattering(kEarth, r, mu, 40, 64, 96);
            CHECK_THAT(coarse.y, WithinRel(fine.y, 0.03));
            CHECK_THAT(coarse.z, WithinRel(fine.z, 0.03));
        }
    }
}

TEST_CASE("irradiance texels span the whole table domain", "[atmosphere][irradiance]") {
    const RadiusMu first = irradianceTexelToRMu(kEarth, 0, 0);
    const RadiusMu last = irradianceTexelToRMu(kEarth, kIrradianceLutWidth - 1, kIrradianceLutHeight - 1);
    CHECK_THAT(first.mu, WithinAbs(-1.0, 1e-12));
    CHECK_THAT(last.mu, WithinAbs(1.0, 1e-12));
    CHECK(first.r > kEarth.bottomRadius);
    CHECK(last.r < kEarth.topRadius);
    CHECK_THAT(last.r - first.r, WithinAbs(kEarth.topRadius - kEarth.bottomRadius - 0.002, 1e-9));
}
