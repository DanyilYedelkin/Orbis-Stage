#pragma once

#include "core/math/Vec3.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace orbis {

/// Perlin's improved noise (2002), output roughly [-1, 1].
///
/// The permutation is shuffled with SplitMix64 + Fisher-Yates rather than std::shuffle (whose
/// algorithm is implementation-defined), so a seed gives the same terrain on every compiler.
struct NoiseSample {
    double value;
    Vec3d gradient; ///< d(value)/dp, analytic.
};

class PerlinNoise {
public:
    explicit PerlinNoise(std::uint64_t seed);

    double sample(const Vec3d& p) const;

    /// Value and analytic gradient (after I. Quilez), for erodedFbm().
    NoiseSample sampleWithGradient(const Vec3d& p) const;

private:
    std::array<std::uint8_t, 512> perm_{};
};

struct FbmParams {
    int octaves = 6;
    double lacunarity = 2.0;
    double gain = 0.5;
    /// ridgedFbm only: crest rounding as a fraction of each octave's wavelength (0 = sharp).
    double crestSoftness = 0.0;
};

/// Octaves above `maxFrequency` fade out (anti-aliasing for coarse LOD levels). Normalised by
/// the amplitude of all octaves, so the large-scale shape is the same at every level.
double fbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params,
           double maxFrequency = std::numeric_limits<double>::infinity());

/// fBm on the unit sphere with each octave damped by 1 / (1 + k |d|^2), d the tangential slope
/// accumulated so far (after I. Quilez): clean flanks, sharp crests, flat valley floors.
double erodedFbm(const PerlinNoise& noise, const Vec3d& unitDirection, double base,
                 const FbmParams& params, double erosion,
                 double maxFrequency = std::numeric_limits<double>::infinity());

/// Ridged multifractal (Musgrave), output [0, 1]. Band-limited and normalised like fbm().
double ridgedFbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params,
                 double maxFrequency = std::numeric_limits<double>::infinity());

} // namespace orbis
