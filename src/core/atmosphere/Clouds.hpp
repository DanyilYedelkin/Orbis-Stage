#pragma once

#include "core/math/Vec3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace orbis {

/// Cumulus layer in the atmosphere model (km): weather x height profile x 3D noise. The GLSL
/// constants are generated from it; the geometry below has twins in clouds.glsl.
struct CloudParameters {
    double bottomAltitude = 2.0;
    double topAltitude = 5.0;
    double coverage = 0.35;   ///< 0 clear .. 1 overcast
    double extinction = 60.0; ///< 1/km at full density
    /// Noise repeats, km. Each divides the previous one, because noise coordinates are offset
    /// by the eye position modulo regionalTile.
    double regionalTile = 60.0;
    double shapeTile = 12.0;
    double detailTile = 1.5;
    // Two Henyey-Greenstein lobes.
    double phaseForward = 0.8;
    double phaseBackward = -0.25;
    double phaseBlend = 0.7; ///< Weight of the forward lobe.
};

/// Where origin + t dir (unit dir) is inside the shell between `inner` and `outer`, clipped to
/// t >= 0 and to the first hit of `ground`. A ray from above that dips below `inner` without
/// reaching the ground crosses the shell twice.
struct ShellIntervals {
    int count = 0;
    double t0[2] = {0.0, 0.0};
    double t1[2] = {0.0, 0.0};
};
ShellIntervals rayShellIntervals(const Vec3d& origin, const Vec3d& dir, double inner, double outer,
                                 double ground);

/// OpenGL cube map convention (spec table 8.19): face 0..5 = +X, -X, +Y, -Y, +Z, -Z, (s, t) in
/// [0, 1] -> unnormalised direction.
Vec3d cubeMapDirection(int face, double s, double t);
void cubeMapLookup(const Vec3d& direction, int& face, double& s, double& t);

/// Cloudiness 0..1 per cube map texel (faces in GL order, rows of increasing t): warped noise on
/// the sphere plus cloudClimatology().
std::vector<float> makeCloudWeather(int faceSize, std::uint64_t seed);

/// Zonal bias (ITCZ, subtropical highs, storm tracks), roughly -0.25..0.2.
double cloudClimatology(double latitude);

/// Throws std::invalid_argument if the noise tiles do not divide each other.
std::string toGlsl(const CloudParameters& p);

} // namespace orbis
