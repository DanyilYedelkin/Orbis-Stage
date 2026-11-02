#pragma once

#include "core/math/Vec3.hpp"

#include <array>
#include <cstdint>
#include <limits>

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
    /// ridgedFbm only: rounds the crests. 0 = razor-sharp (a slope discontinuity along every
    /// zero line of the noise); ~0.1-0.2 = rounded over that fraction of each octave's
    /// wavelength, i.e. equally soft at every scale. Also smooths the octave weighting.
    double crestSoftness = 0.0;
};

/// Fractional Brownian motion: a sum of noise octaves, each finer and weaker. Normalised by the
/// total amplitude so the output stays roughly in [-1, 1] for any octave count.
///
/// `maxFrequency` band-limits the sum (anti-aliasing): octaves above it are faded out, so a coarse
/// LOD level does not sample detail it cannot represent (which would shimmer and pop). The
/// normalisation always uses ALL octaves, so the low frequencies - the overall shape - are
/// identical at every level of detail.
double fbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params,
           double maxFrequency = std::numeric_limits<double>::infinity());

/// Ridged multifractal (F. K. Musgrave): mountain ranges with sharp crests and smooth valleys.
/// Each octave is (1 - |noise|)^2 - a crest where smooth noise crosses zero - and is weighted by
/// the previous octave's signal, so detail piles up on ridges and stays out of valley floors.
/// Output in [0, 1]. Band-limited and normalised like fbm().
double ridgedFbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params,
                 double maxFrequency = std::numeric_limits<double>::infinity());

} // namespace orbis
