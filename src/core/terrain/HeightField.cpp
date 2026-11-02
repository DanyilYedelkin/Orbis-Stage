#include "core/terrain/HeightField.hpp"

#include <algorithm>
#include <cmath>

namespace orbis {

namespace {
constexpr double kOceanDepth = 7000.0;     // metres at the deepest continent value
constexpr double kLowlandHeight = 2200.0;  // broad continental relief
constexpr double kMountainHeight = 4300.0; // extra height on ridges
constexpr double kSeaLevelBias = -0.04;    // shifts the land/ocean ratio towards more ocean
} // namespace

HeightField::HeightField(std::uint64_t seed)
    : continents_(seed), mountains_(seed ^ 0xA5A5A5A5DEADBEEFull) {}

double HeightField::elevation(const Vec3d& dir) const {
    // Low frequency: where the continents are.
    const double continent =
        fbm(continents_, dir * 1.7, {.octaves = 6, .lacunarity = 2.0, .gain = 0.5}) +
        kSeaLevelBias;

    if (continent < 0.0) {
        return continent * kOceanDepth / 0.5;
    }

    // "Ridged" noise: 1 - |n| turns the zero crossings of smooth noise into sharp crests.
    const double n = fbm(mountains_, dir * 7.0, {.octaves = 6, .lacunarity = 2.1, .gain = 0.5});
    const double ridge = 1.0 - std::abs(n);
    // Mountains fade in away from the coast so beaches are not cliffs.
    const double inland = std::clamp((continent - 0.02) * 8.0, 0.0, 1.0);

    return continent * kLowlandHeight / 0.5 + ridge * ridge * kMountainHeight * inland;
}

} // namespace orbis
