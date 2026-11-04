#pragma once

#include "core/math/Vec3.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace orbis {

/// A layer of cumulus-type clouds: a spherical shell in the atmosphere model (km), filled with
/// a density field = weather (where there are clouds) x height profile x 3D noise (their shape).
///
/// Like AtmosphereParameters this is the single source of truth: the GLSL constants are
/// generated from it, and the geometric functions below have GLSL twins in clouds.glsl.
struct CloudParameters {
    double bottomAltitude = 2.0; ///< km above sea level: cumulus bases sit at 1-2.5 km.
    double topAltitude = 5.0;    ///< km
    double coverage = 0.35;      ///< 0 = clear sky, 1 = overcast (shifts the weather threshold).
    double extinction = 60.0;    ///< 1/km at full density (cumulus: ~0.05-0.1 per metre).
    /// km per repeat of the 3D noise, as used for: regional patterns that break up the
    /// repetition of the shapes; the cloud shapes; their eroded detail. Each must divide the
    /// previous one: the eye position is passed modulo regionalTile, which keeps all three
    /// world-anchored (tested).
    double regionalTile = 60.0;
    double shapeTile = 12.0;
    double detailTile = 1.5;
    double phaseForward = 0.8;   ///< Two Henyey-Greenstein lobes: bright silver lining...
    double phaseBackward = -0.25; ///< ...and a weaker back-scattering glow.
    double phaseBlend = 0.7;     ///< Weight of the forward lobe.
};

/// Distances along origin + t * dir (unit dir) where the ray is inside the shell between radii
/// `inner` and `outer` (centred at the origin of the coordinate system): up to two intervals -
/// a ray from above that dips through the shell and leaves it again on the far side of the
/// planet's "inner sphere" crosses it twice. Intervals are clipped to t >= 0 and to the near hit
/// of `ground` (the ray stops at the planet). count = number of non-empty intervals.
struct ShellIntervals {
    int count = 0;
    double t0[2] = {0.0, 0.0};
    double t1[2] = {0.0, 0.0};
};
ShellIntervals rayShellIntervals(const Vec3d& origin, const Vec3d& dir, double inner, double outer,
                                 double ground);

/// OpenGL cube map convention (spec table 8.19): face index 0..5 = +X, -X, +Y, -Y, +Z, -Z and
/// texture coordinates (s, t) in [0, 1] -> direction (not normalised). The cloud weather map is
/// a cube map so every direction on the sphere gets equally sized texels, with no poles.
Vec3d cubeMapDirection(int face, double s, double t);
/// Inverse: direction -> face and (s, t) in [0, 1].
void cubeMapLookup(const Vec3d& direction, int& face, double& s, double& t);

/// Planet-wide "weather": cloudiness in [0, 1] per cube map texel (faceSize^2 x 6, faces in GL
/// order, rows of increasing t). Noise on the unit sphere (systems of ~3000 km down to ~30 km),
/// biased by a zonal climatology: the cloudy tropical convergence zone, the clear subtropical
/// highs, the stormy mid-latitudes. Deterministic for a seed.
std::vector<float> makeCloudWeather(int faceSize, std::uint64_t seed);

/// The zonal climatology alone: cloudiness bias for a latitude (radians), roughly -0.25..+0.2.
double cloudClimatology(double latitude);

/// GLSL constants for the cloud layer (prepended to clouds.glsl). Throws std::invalid_argument
/// if the noise tiles do not divide each other.
std::string toGlsl(const CloudParameters& p);

} // namespace orbis
