#include "core/terrain/DetailNoise.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace orbis {

namespace {

std::uint64_t splitMix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// Unit gradient for a lattice point (already wrapped). Hash -> angle: isotropic, no axis bias.
void gradientAt(int i, int j, int period, std::uint64_t seed, double& gx, double& gy) {
    constexpr std::uint64_t kPrime = 0x100000001B3u;
    const auto ui = static_cast<std::uint64_t>(i);
    const auto uj = static_cast<std::uint64_t>(j);
    const auto up = static_cast<std::uint64_t>(period);
    const std::uint64_t h = splitMix64(seed ^ (ui * kPrime) ^ (uj << 32) ^ up);
    const double angle = static_cast<double>(h >> 11) * (2.0 * std::numbers::pi / 9007199254740992.0);
    gx = std::cos(angle);
    gy = std::sin(angle);
}

int wrap(int i, int period) {
    const int m = i % period;
    return m < 0 ? m + period : m;
}

} // namespace

PeriodicNoiseSample periodicGradientNoise(double u, double v, int period, std::uint64_t seed) {
    const double x = u * period;
    const double y = v * period;
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double tx = x - fx;
    const double ty = y - fy;
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);

    // Quintic fade (C2) and its derivative.
    const auto fade = [](double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };
    const auto dfade = [](double t) { return 30.0 * t * t * (t * (t - 2.0) + 1.0); };
    const double sx = fade(tx);
    const double sy = fade(ty);
    const double dsx = dfade(tx);
    const double dsy = dfade(ty);

    double g[4][2];
    for (int k = 0; k < 4; ++k) {
        gradientAt(wrap(ix + (k & 1), period), wrap(iy + (k >> 1), period), period, seed, g[k][0],
                   g[k][1]);
    }
    // Corner contributions: dot(gradient, offset to the corner).
    const double n00 = g[0][0] * tx + g[0][1] * ty;
    const double n10 = g[1][0] * (tx - 1.0) + g[1][1] * ty;
    const double n01 = g[2][0] * tx + g[2][1] * (ty - 1.0);
    const double n11 = g[3][0] * (tx - 1.0) + g[3][1] * (ty - 1.0);

    const double a = n00;
    const double b = n10 - n00;
    const double c = n01 - n00;
    const double d = n11 - n10 - n01 + n00;
    const double value = a + b * sx + c * sy + d * sx * sy;

    // d/dx = interpolated corner gradients + change of the interpolation weights.
    const double gxi = g[0][0] + (g[1][0] - g[0][0]) * sx + (g[2][0] - g[0][0]) * sy +
                       (g[3][0] - g[2][0] - g[1][0] + g[0][0]) * sx * sy;
    const double gyi = g[0][1] + (g[1][1] - g[0][1]) * sx + (g[2][1] - g[0][1]) * sy +
                       (g[3][1] - g[2][1] - g[1][1] + g[0][1]) * sx * sy;
    const double ddx = gxi + (b + d * sy) * dsx;
    const double ddy = gyi + (c + d * sx) * dsy;
    // Lattice units -> tile units.
    return {value, ddx * period, ddy * period};
}

DetailNoise makeDetailNoise(int size, std::uint64_t seed) {
    if (size < 64 || (size & (size - 1)) != 0) {
        throw std::invalid_argument("makeDetailNoise: size must be a power of two >= 64");
    }
    DetailNoise out;
    out.size = size;
    out.texels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);

    // Height: 6 octaves from 4 to 128 lattice cells per tile (the finest has 4+ texels per cell
    // at 512^2, so it is still resolved). Variation: 4 coarser octaves, independent seed.
    struct Octave {
        int period;
        double amplitude;
    };
    constexpr Octave height[] = {{4, 1.0}, {8, 0.55}, {16, 0.30}, {32, 0.17}, {64, 0.09}, {128, 0.05}};
    constexpr Octave variation[] = {{2, 1.0}, {4, 0.6}, {8, 0.35}, {16, 0.2}};

    std::vector<double> raw(out.texels.size());
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double u = (x + 0.5) / size;
            const double v = (y + 0.5) / size;
            double h = 0.0;
            double du = 0.0;
            double dv = 0.0;
            std::uint64_t octaveSeed = seed;
            for (const Octave& o : height) {
                const PeriodicNoiseSample s = periodicGradientNoise(u, v, o.period, octaveSeed);
                h += o.amplitude * s.value;
                du += o.amplitude * s.du;
                dv += o.amplitude * s.dv;
                octaveSeed = splitMix64(octaveSeed);
            }
            double var = 0.0;
            std::uint64_t variationSeed = splitMix64(seed ^ 0xA5A5A5A5DEADBEEFull);
            for (const Octave& o : variation) {
                var += o.amplitude * periodicGradientNoise(u, v, o.period, variationSeed).value;
                variationSeed = splitMix64(variationSeed);
            }
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
            raw[i] = h;
            raw[i + 1] = du;
            raw[i + 2] = dv;
            raw[i + 3] = var;
        }
    }

    // Measured, so the shader's amplitudes are standard deviations.
    for (int channel : {0, 3}) {
        double sum = 0.0;
        double sumSq = 0.0;
        const std::size_t n = raw.size() / 4;
        for (std::size_t i = 0; i < n; ++i) {
            sum += raw[i * 4 + static_cast<std::size_t>(channel)];
        }
        const double mean = sum / static_cast<double>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double d = raw[i * 4 + static_cast<std::size_t>(channel)] - mean;
            sumSq += d * d;
        }
        const double invStd = 1.0 / std::sqrt(sumSq / static_cast<double>(n));
        for (std::size_t i = 0; i < n; ++i) {
            double& value = raw[i * 4 + static_cast<std::size_t>(channel)];
            value = (value - mean) * invStd;
            if (channel == 0) {
                raw[i * 4 + 1] *= invStd;
                raw[i * 4 + 2] *= invStd;
            }
        }
    }
    double gradientSq = 0.0;
    for (std::size_t i = 0; i < raw.size(); i += 4) {
        gradientSq += raw[i + 1] * raw[i + 1] + raw[i + 2] * raw[i + 2];
    }
    out.gradientStd = std::sqrt(gradientSq / static_cast<double>(raw.size() / 2));
    for (std::size_t i = 0; i < raw.size(); ++i) {
        out.texels[i] = static_cast<float>(raw[i]);
    }
    return out;
}

} // namespace orbis
