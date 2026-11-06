#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orbis {

/// Tiling detail texture: height fBm (mean 0, std 1), its analytic du and dv, and an independent
/// lower-frequency fBm for albedo. Analytic derivatives stay correct through the mip chain,
/// which finite differences of the averaged height would not.
struct DetailNoise {
    int size = 0;
    std::vector<float> texels; ///< RGBA, rows of increasing v.
    /// Of d height/du, in tile units. Lets the renderer specify bumps as slopes.
    double gradientStd = 0.0;

    double at(int x, int y, int channel) const {
        return static_cast<double>(texels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                       static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(channel)]);
    }
};

/// `size`: power of two, >= 64.
DetailNoise makeDetailNoise(int size, std::uint64_t seed);

/// One octave, `period` lattice cells per tile.
struct PeriodicNoiseSample {
    double value;
    double du;
    double dv;
};
PeriodicNoiseSample periodicGradientNoise(double u, double v, int period, std::uint64_t seed);

} // namespace orbis
