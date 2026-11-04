#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orbis {

/// A square, seamlessly TILING noise texture for per-pixel surface detail (CPU-generated once,
/// uploaded with mipmaps). Four channels per texel, all in texture units (u, v in [0, 1) per
/// tile):
///   0  height       fBm, normalised to mean 0 / standard deviation 1
///   1  d height/du  analytic derivative (not finite differences), so bump mapping is exact and
///   2  d height/dv  the mip chain (a linear average) stays the derivative of the averaged height
///   3  variation    an independent, lower-frequency fBm (mean 0 / std 1) for albedo variation
///
/// Tiling comes from periodic gradient noise: lattice coordinates wrap modulo each octave's
/// period, and every period divides the texture, so the right edge continues into the left one.
/// The renderer samples it at several world scales that all divide one common period; that is
/// what lets it anchor the detail to the WORLD with only small float coordinates on the GPU.
struct DetailNoise {
    int size = 0;
    std::vector<float> texels; ///< size * size * 4, row-major (row = v, column = u).
    /// Measured standard deviation of d height/du (= of d/dv: the noise is isotropic), in tile
    /// units. Renderers divide by it to specify bumps as SLOPES, independent of the octaves.
    double gradientStd = 0.0;

    double at(int x, int y, int channel) const {
        return static_cast<double>(texels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                       static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(channel)]);
    }
};

/// `size` must be a power of two >= 64. Deterministic for a given seed on every platform.
DetailNoise makeDetailNoise(int size, std::uint64_t seed);

/// One octave of periodic 2D gradient noise at (u, v) in tile units, `period` lattice cells per
/// tile: value and analytic derivatives d/du, d/dv. Exposed for tests.
struct PeriodicNoiseSample {
    double value;
    double du;
    double dv;
};
PeriodicNoiseSample periodicGradientNoise(double u, double v, int period, std::uint64_t seed);

} // namespace orbis
