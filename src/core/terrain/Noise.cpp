#include "core/terrain/Noise.hpp"

#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>

namespace orbis {

namespace {

std::uint64_t splitMix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// Quintic fade 6t^5 - 15t^4 + 10t^3: zero 1st and 2nd derivatives at 0 and 1, which removes
/// the visible grid artefacts of the original (cubic) Perlin noise.
double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

double lerp(double t, double a, double b) { return a + t * (b - a); }

/// Dot product with one of 12 gradient directions (cube edge midpoints), picked by the hash.
double grad(int hash, double x, double y, double z) {
    const int h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

} // namespace

PerlinNoise::PerlinNoise(std::uint64_t seed) {
    std::array<std::uint8_t, 256> p{};
    std::iota(p.begin(), p.end(), std::uint8_t{0});

    std::uint64_t state = seed;
    for (std::size_t i = p.size() - 1; i > 0; --i) {
        const auto j = static_cast<std::size_t>(splitMix64(state) % (i + 1));
        std::swap(p[i], p[j]);
    }
    // Duplicated so lookups like perm[perm[x] + y] never need a modulo.
    for (std::size_t i = 0; i < perm_.size(); ++i) {
        perm_[i] = p[i & 255u];
    }
}

double PerlinNoise::sample(const Vec3d& pt) const {
    const double fx = std::floor(pt.x);
    const double fy = std::floor(pt.y);
    const double fz = std::floor(pt.z);

    // Lattice cell (wrapped to 0..255; C++20 guarantees two's complement for negatives).
    const int xi = static_cast<int>(fx) & 255;
    const int yi = static_cast<int>(fy) & 255;
    const int zi = static_cast<int>(fz) & 255;

    // Position inside the cell, [0, 1).
    const double x = pt.x - fx;
    const double y = pt.y - fy;
    const double z = pt.z - fz;

    const double u = fade(x);
    const double v = fade(y);
    const double w = fade(z);

    const auto P = [this](int i) { return static_cast<int>(perm_[static_cast<std::size_t>(i)]); };
    const int a = P(xi) + yi;
    const int aa = P(a) + zi;
    const int ab = P(a + 1) + zi;
    const int b = P(xi + 1) + yi;
    const int ba = P(b) + zi;
    const int bb = P(b + 1) + zi;

    // Trilinear blend of the 8 corner contributions.
    return lerp(w,
                lerp(v, lerp(u, grad(P(aa), x, y, z), grad(P(ba), x - 1, y, z)),
                     lerp(u, grad(P(ab), x, y - 1, z), grad(P(bb), x - 1, y - 1, z))),
                lerp(v, lerp(u, grad(P(aa + 1), x, y, z - 1), grad(P(ba + 1), x - 1, y, z - 1)),
                     lerp(u, grad(P(ab + 1), x, y - 1, z - 1),
                          grad(P(bb + 1), x - 1, y - 1, z - 1))));
}

double fbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double totalAmplitude = 0.0;
    for (int i = 0; i < params.octaves; ++i) {
        sum += amplitude * noise.sample(p * frequency);
        totalAmplitude += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return totalAmplitude > 0.0 ? sum / totalAmplitude : 0.0;
}

} // namespace orbis
