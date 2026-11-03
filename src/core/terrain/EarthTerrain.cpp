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
constexpr double kAlpineWavelength = 8000.0; // metres: peak-to-peak spacing of real ranges
// Standard deviation of the synthetic mountain relief: a base for any real range plus a share of
// the measured relief, capped. A purely proportional rule over-steepened broad ranges (Alps: 46 %
// of the area above 30 degrees) while leaving narrow ones tame (Tatras: 7 %).
constexpr double kAlpineBaseStd = 250.0;
constexpr double kAlpineReliefGain = 0.45;
constexpr double kAlpineMaxStd = 1000.0;

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
    const double roughness = 20.0 + 0.03 * height + 0.15 * relief;

    // Mountain relief at the scale the data cannot show: individual peaks and valleys a few km
    // apart, as tall as the measured relief suggests. Eroded noise gives long clean flanks,
    // sharp crests and flat valley floors; soft ridged noise adds continuous crest lines.
    // Zero-mean, so valleys get deeper as much as peaks get higher.
    // (Measured target: high Alps ~25% of the area steeper than 30 degrees.)
    double alpine = 0.0;
    if (mountains > 0.0) {
        const double alpineBase = kMeanRadius / kAlpineWavelength;
        const double maxAlpine = maxFrequencyOnSphere / alpineBase;
        const double eroded = erodedFbm(alpine_, dir, alpineBase, kErodedParams, 1.0, maxAlpine);
        const double crests =
            ridgedFbm(alpine_, dir * (alpineBase * 0.5), kCrestParams, maxAlpine * 2.0);
        // Both layers normalised to mean 0, standard deviation 1, so `std` below is literally
        // the standard deviation of the synthetic relief (constants verified by a test).
        const double layers = 0.8 * (eroded - kErodedMean) / kErodedStd +
                              0.6 * (crests - kCrestMean) / kCrestStd;
        const double std = std::min(kAlpineBaseStd + kAlpineReliefGain * relief, kAlpineMaxStd);
        alpine = layers * std * mountains;
    }

    const double result = measured + landFactor * (detail * roughness + alpine);
    // Where the DATA says land, synthetic detail must not create sea: otherwise every lowland a
    // few metres above sea level fills with puddles. (The other way round is allowed and gives
    // natural, fractal coastlines: shallow sea floor may rise into small islands.)
    return measured > 0.0 ? std::max(result, 0.5) : result;
}

} // namespace orbis
