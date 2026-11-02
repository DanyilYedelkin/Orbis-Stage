#include "core/terrain/EarthTerrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace orbis {

namespace {
constexpr double kMeanRadius = 6371000.0;
constexpr int kReliefSmoothing = 2; // relief = |data - its 4x smoothed version|
// The relief map is read with a little extra smoothing (2x) to fill the thin zero lines of
// |a - b|. More would dilute narrow ranges: reading at level 2 once shrank the Tatras' relief
// from ~320 m to ~210 m and flattened them (caught by a test).
constexpr double kReliefReadLevel = 1.0;

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
} // namespace

EarthTerrain::EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed)
    : grid_(std::move(grid)), relief_(grid_.reliefMap(kReliefSmoothing)), ellipsoid_(ellipsoid),
      dataSpacing_(grid_.nodeSpacing(0, kMeanRadius)), detail_(seed),
      alpine_(seed ^ 0x5DEECE66DF00D123ull) {}

double EarthTerrain::elevation(const Vec3d& dir, double sampleSpacing) const {
    // Direction from the centre -> point on the ellipsoid -> GEODETIC latitude, which is what
    // the grid is indexed by. For a point on the surface: tan(lat) = z / ((1 - e^2) * p).
    const Vec3d p = ellipsoid_.surfacePointAlong(dir);
    const double horizontal = std::sqrt(p.x * p.x + p.y * p.y);
    const double latitude = std::atan2(p.z, (1.0 - ellipsoid_.eccentricitySquared()) * horizontal);
    const double longitude = std::atan2(p.y, p.x);

    // 1. Measured relief, pre-filtered to what the mesh can show: if vertices are 4 data cells
    //    apart, read from the mip level whose nodes are 4 cells apart (level 2).
    const double level = sampleSpacing > 0.0 ? std::max(std::log2(sampleSpacing / dataSpacing_), 0.0)
                                             : 0.0;
    const double measured = grid_.sample(latitude, longitude, level);

    const double landFactor = smoothstep(-300.0, -50.0, measured);
    if (landFactor <= 0.0) {
        return measured; // ocean floor is never visible (water is flat), skip the noise
    }

    // 2. Synthetic detail, only BELOW the data resolution: the first octave has a wavelength of
    //    two data cells, so it never fights with the measured shapes.
    const double maxFrequencyOnSphere = sampleSpacing > 0.0
                                            ? kMeanRadius / (2.0 * sampleSpacing)
                                            : std::numeric_limits<double>::infinity();
    const double base = kMeanRadius / (2.0 * dataSpacing_);
    const double detail = fbm(detail_, dir * base, {.octaves = 16, .lacunarity = 2.0, .gain = 0.55},
                              maxFrequencyOnSphere / base);

    // Where are the mountains, and how big? Absolute height is a poor guide (the Tatras are
    // "only" ~2 km in the data, the Tibetan plateau is high but flat). The precomputed relief map
    // says how much the data deviates from its own smoothed version - smoothly, everywhere:
    // ~5 m on plains, ~300 m in the Tatras, ~700 m in the Alps.
    const double height = std::max(measured, 0.0);
    const double relief =
        relief_.sample(latitude, longitude, std::max(level, kReliefReadLevel));
    const double mountains = smoothstep(80.0, 400.0, relief);
    const double roughness = 25.0 + 0.04 * height + 0.25 * relief;

    // Ridged crests and valleys, as strong as the measured relief suggests. Centred around zero
    // (minus the typical ridged value) so valleys get deeper as well as peaks higher.
    double alpine = 0.0;
    if (mountains > 0.0) {
        const double ridgeBase = base * 2.0;
        const double amplitude = std::min(600.0 + 1.2 * relief, 2400.0);
        // Rounded crests: at Earth's moderate amplitudes razor-sharp ones read as kilometre-wide
        // "disks" in the lighting rather than as ridges (found by isolating the terms).
        alpine = (ridgedFbm(alpine_, dir * ridgeBase,
                            {.octaves = 9, .lacunarity = 2.0, .gain = 0.5, .crestSoftness = 0.15},
                            maxFrequencyOnSphere / ridgeBase) -
                  0.25) *
                 amplitude * mountains;
    }

    const double result = measured + landFactor * (detail * roughness + alpine);
    // Where the DATA says land, synthetic detail must not create sea: otherwise every lowland a
    // few metres above sea level fills with puddles. (The other way round is allowed and gives
    // natural, fractal coastlines: shallow sea floor may rise into small islands.)
    return measured > 0.0 ? std::max(result, 0.5) : result;
}

} // namespace orbis
