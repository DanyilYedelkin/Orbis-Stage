#pragma once

#include "core/math/Vec3.hpp"

#include <array>
#include <cstdint>

namespace orbis {

/// Ken Perlin's "improved noise" (2002) in 3D. Smooth, band-limited, output roughly [-1, 1].
///
/// Deterministic across compilers and platforms: the permutation table is shuffled with our own
/// SplitMix64 + Fisher-Yates instead of std::shuffle, whose algorithm is implementation-defined.
/// Same seed => bit-identical terrain on MSVC and GCC, which matters for networked or replayed
/// simulations where every machine must agree on the world.
class PerlinNoise {
public:
    explicit PerlinNoise(std::uint64_t seed);

    double sample(const Vec3d& p) const;

private:
    std::array<std::uint8_t, 512> perm_{};
};

struct FbmParams {
    int octaves = 6;
    double lacunarity = 2.0; ///< Frequency multiplier per octave.
    double gain = 0.5;       ///< Amplitude multiplier per octave.
};

/// Fractional Brownian motion: a sum of noise octaves, each finer and weaker. Normalised by the
/// total amplitude so the output stays roughly in [-1, 1] for any octave count.
double fbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params);

} // namespace orbis
