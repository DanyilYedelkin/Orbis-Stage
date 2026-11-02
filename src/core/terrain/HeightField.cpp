#include "core/terrain/HeightField.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orbis {

namespace {
constexpr double kPlanetRadius = 6371000.0;   // only used to convert spacing -> frequency
constexpr double kOceanDepth = 7000.0;        // metres at the deepest continent value
constexpr double kLowlandHeight = 1500.0;     // broad continental relief
constexpr double kRangeHeight = 1000.0;       // uplift of a whole mountain range
constexpr double kAlpineHeight = 4200.0;      // peaks and valleys inside a range
constexpr double kPlainsRoughness = 120.0;    // fractal detail amplitude on flat land
constexpr double kMountainRoughness = 500.0;  // ... and in the mountains
constexpr double kSeaLevelBias = -0.04;       // shifts the land/ocean ratio towards more ocean
} // namespace

HeightField::HeightField(std::uint64_t seed)
    : continents_(seed), ranges_(seed ^ 0xA5A5A5A5DEADBEEFull),
      alpine_(seed ^ 0x5DEECE66DF00D123ull), detail_(seed ^ 0x0123456789ABCDEFull) {}

double HeightField::elevation(const Vec3d& dir, double sampleSpacing) const {
    // Moving by an angle t on the unit sphere moves p = dir * base by base * t, i.e. one noise
    // period is kPlanetRadius / base metres. Nyquist: wavelength >= 2 * spacing.
    const double maxFrequencyOnSphere = sampleSpacing > 0.0
                                            ? kPlanetRadius / (2.0 * sampleSpacing)
                                            : std::numeric_limits<double>::infinity();
    const auto layer = [&](const PerlinNoise& noise, double base, const FbmParams& params,
                           bool ridged = false) {
        const double maxFrequency = maxFrequencyOnSphere / base;
        return ridged ? ridgedFbm(noise, dir * base, params, maxFrequency)
                      : fbm(noise, dir * base, params, maxFrequency);
    };

    // Low frequency: where the continents are (~3700 km features).
    const double continent =
        layer(continents_, 1.7, {.octaves = 6, .lacunarity = 2.0, .gain = 0.5}) + kSeaLevelBias;

    if (continent < 0.0) {
        return continent * kOceanDepth / 0.5;
    }

    // WHERE the mountain ranges run: crest lines of a low-frequency noise ("ridged": 1 - |n|),
    // faded in away from the coast so beaches are not cliffs.
    const double n = layer(ranges_, 7.0, {.octaves = 4, .lacunarity = 2.1, .gain = 0.5});
    const double range = 1.0 - std::abs(n);
    const double inland = std::clamp((continent - 0.02) * 8.0, 0.0, 1.0);
    const double mountainMask = range * range * inland;

    // WHAT the mountains look like: ridged multifractal, ~21 km down to ~80 m features.
    const double alpine =
        mountainMask > 0.0
            ? layer(alpine_, 300.0, {.octaves = 9, .lacunarity = 2.0, .gain = 0.5}, true)
            : 0.0;

    // Fractal detail from ~160 km down to ~1 m. gain 0.58 (> 0.5) keeps small scales rough
    // enough to be interesting at ground level. Faded in over the first metres above sea level
    // so the function stays continuous at the coastline.
    const double detail = layer(detail_, 40.0, {.octaves = 18, .lacunarity = 2.0, .gain = 0.58});
    const double coast = std::clamp(continent * 20.0, 0.0, 1.0);
    const double roughness = kPlainsRoughness + kMountainRoughness * mountainMask;

    return continent * kLowlandHeight / 0.5 + mountainMask * (kRangeHeight + alpine * kAlpineHeight) +
           detail * roughness * coast;
}

} // namespace orbis
