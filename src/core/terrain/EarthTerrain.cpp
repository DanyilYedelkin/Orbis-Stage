#include "core/terrain/EarthTerrain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace orbis {

namespace {
constexpr double kMeanRadius = 6371000.0;
constexpr int kReliefSmoothing = 2;
// Level 1 fills the zero lines of |a - b|; level 2 already dilutes narrow ranges like the Tatras.
constexpr double kReliefReadLevel = 1.0;
constexpr double kAlpineWavelength = 8000.0; // peak-to-peak spacing of real ranges
// Std of the synthetic relief. Purely proportional to the measured relief, broad ranges came out
// too steep and narrow ones too tame.
constexpr double kAlpineBaseStd = 250.0;
constexpr double kAlpineReliefGain = 0.45;
constexpr double kAlpineMaxStd = 1000.0;
// The relief thresholds and roughness below were set on 10' data (18.5 km nodes).
constexpr double kReferenceSpacing = 18530.0;
// Relief missing below the data resolution shrinks like wavelength^H. H = 1 puts 35 % of the
// Valais above 30 degrees with 1' data (0.9: 43 %, 0.7: 59 %).
constexpr double kHurst = 1.0;

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
} // namespace

EarthTerrain::EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed,
                           std::shared_ptr<const HighResolutionTerrain> highResolution)
    : grid_(std::move(grid)), dataSpacing_(grid_.nodeSpacing(0, kMeanRadius)),
      reliefBase_(std::clamp(
          static_cast<int>(std::lround(std::log2(kReferenceSpacing / dataSpacing_))), 0,
          grid_.levelCount() - 1)),
      relief_(grid_.reliefMap(kReliefSmoothing, reliefBase_)), ellipsoid_(ellipsoid),
      alpineWavelength_(std::min(kAlpineWavelength, dataSpacing_)),
      alpineScale_(std::pow(alpineWavelength_ / kAlpineWavelength, kHurst)),
      roughnessScale_(std::min(std::pow(dataSpacing_ / kReferenceSpacing, kHurst), 1.0)),
      detail_(seed), alpine_(seed ^ 0x5DEECE66DF00D123ull),
      highResolution_(std::move(highResolution)) {}

EarthTerrain::EarthTerrain(ElevationGrid grid, const Ellipsoid& ellipsoid, std::uint64_t seed,
                           std::optional<ElevationGrid> highResolution)
    : EarthTerrain(std::move(grid), ellipsoid, seed,
                   highResolution ? std::make_shared<const HgtMosaicTerrain>(std::move(*highResolution))
                                  : std::shared_ptr<const HighResolutionTerrain>{}) {}

double EarthTerrain::elevation(const Vec3d& dir, double sampleSpacing) const {
    // Grids are indexed by geodetic latitude: tan(lat) = z / ((1 - e^2) p) on the surface.
    const Vec3d p = ellipsoid_.surfacePointAlong(dir);
    const double horizontal = std::sqrt(p.x * p.x + p.y * p.y);
    const double latitude = std::atan2(p.z, (1.0 - ellipsoid_.eccentricitySquared()) * horizontal);
    const double longitude = std::atan2(p.y, p.x);

    if (highResolution_) {
        const auto real = highResolution_->sample(latitude, longitude, sampleSpacing);
        if (real && real->weight > 0.0) {
            const double maxFrequency = sampleSpacing > 0.0
                                            ? kMeanRadius / (2.0 * sampleSpacing)
                                            : std::numeric_limits<double>::infinity();
            const double base = kMeanRadius / (2.0 * real->spacing);
            const double fine = fbm(detail_, dir * base, {.octaves = 10, .lacunarity = 2.0, .gain = 0.5},
                                    maxFrequency / base) *
                                (2.0 + 0.05 * real->spacing);
            const double detailed = real->height + fine;
            if (real->weight >= 1.0) {
                return detailed;
            }
            const double outside = baseElevation(dir, latitude, longitude, sampleSpacing);
            return outside + real->weight * (detailed - outside);
        }
    }
    return baseElevation(dir, latitude, longitude, sampleSpacing);
}

double EarthTerrain::baseElevation(const Vec3d& dir, double latitude, double longitude,
                                   double sampleSpacing) const {
    const double level = sampleSpacing > 0.0 ? std::max(std::log2(sampleSpacing / dataSpacing_), 0.0)
                                             : 0.0;
    const double measured = grid_.sample(latitude, longitude, level);

    const double landFactor = smoothstep(-300.0, -50.0, measured);
    if (landFactor <= 0.0) {
        return measured; // the sea floor is never visible
    }

    // The first octave has a wavelength of two data cells.
    const double maxFrequencyOnSphere = sampleSpacing > 0.0
                                            ? kMeanRadius / (2.0 * sampleSpacing)
                                            : std::numeric_limits<double>::infinity();
    const double base = kMeanRadius / (2.0 * dataSpacing_);
    const double detail = fbm(detail_, dir * base, {.octaves = 16, .lacunarity = 2.0, .gain = 0.55},
                              maxFrequencyOnSphere / base);

    // Relief, not height, says where the mountains are (Tibet is high but flat): ~5 m on
    // plains, ~300 m in the Tatras, ~700 m in the Alps.
    const double height = std::max(measured, 0.0);
    const double relief = relief_.sample(latitude, longitude,
                                         std::max(level - reliefBase_, kReliefReadLevel));
    const double mountains = smoothstep(80.0, 400.0, relief) * alpineScale_;
    const double roughness = (20.0 + 0.03 * height + 0.15 * relief) * roughnessScale_;

    // Peaks a few km apart: eroded fBm for clean flanks and flat valley floors, soft ridged
    // noise for crest lines. Zero-mean, so valleys deepen as much as peaks rise.
    double alpine = 0.0;
    if (mountains > 0.0) {
        const double alpineBase = kMeanRadius / alpineWavelength_;
        const double maxAlpine = maxFrequencyOnSphere / alpineBase;
        const double eroded = erodedFbm(alpine_, dir, alpineBase, kErodedParams, 1.0, maxAlpine);
        const double crests =
            ridgedFbm(alpine_, dir * (alpineBase * 0.5), kCrestParams, maxAlpine * 2.0);
        const double layers = 0.8 * (eroded - kErodedMean) / kErodedStd +
                              0.6 * (crests - kCrestMean) / kCrestStd;
        const double std = std::min(kAlpineBaseStd + kAlpineReliefGain * relief, kAlpineMaxStd);
        alpine = layers * std * mountains;
    }

    const double offset = landFactor * (detail * roughness + alpine);
    // Detail must not turn measured land into sea (lowlands would fill with puddles); the other
    // way round gives fractal coastlines. The floor is soft: a hard clamp flattened deep
    // synthetic valleys into terraces at sea level.
    if (measured > 0.0 && offset < 0.0) {
        const double room = std::max(measured - 0.5, 1e-3);
        return measured - room * (1.0 - std::exp(offset / room));
    }
    const double result = measured + offset;
    return measured > 0.0 ? std::max(result, 0.5) : result;
}

} // namespace orbis
