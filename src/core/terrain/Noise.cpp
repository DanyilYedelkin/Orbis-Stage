#include "core/terrain/Noise.hpp"

#include <algorithm>
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

/// Quintic: C2, no grid artefacts of the cubic fade.
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

    return lerp(w,
                lerp(v, lerp(u, grad(P(aa), x, y, z), grad(P(ba), x - 1, y, z)),
                     lerp(u, grad(P(ab), x, y - 1, z), grad(P(bb), x - 1, y - 1, z))),
                lerp(v, lerp(u, grad(P(aa + 1), x, y, z - 1), grad(P(ba + 1), x - 1, y, z - 1)),
                     lerp(u, grad(P(ab + 1), x, y - 1, z - 1),
                          grad(P(bb + 1), x - 1, y - 1, z - 1))));
}

NoiseSample PerlinNoise::sampleWithGradient(const Vec3d& pt) const {
    const double fx = std::floor(pt.x);
    const double fy = std::floor(pt.y);
    const double fz = std::floor(pt.z);
    const int xi = static_cast<int>(fx) & 255;
    const int yi = static_cast<int>(fy) & 255;
    const int zi = static_cast<int>(fz) & 255;
    const double x = pt.x - fx;
    const double y = pt.y - fy;
    const double z = pt.z - fz;

    // Fade and its derivative: 6t^5 - 15t^4 + 10t^3  ->  30 t^2 (t - 1)^2.
    const auto dfade = [](double t) { return 30.0 * t * t * (t - 1.0) * (t - 1.0); };
    const double u = fade(x), v = fade(y), w = fade(z);
    const double du = dfade(x), dv = dfade(y), dw = dfade(z);

    const auto P = [this](int i) { return static_cast<int>(perm_[static_cast<std::size_t>(i)]); };
    const int a = P(xi) + yi, aa = P(a) + zi, ab = P(a + 1) + zi;
    const int b = P(xi + 1) + yi, ba = P(b) + zi, bb = P(b + 1) + zi;

    // grad() is linear in its offset, so its gradient vector is grad() of the unit axes.
    const auto gvec = [](int hash) {
        return Vec3d{grad(hash, 1, 0, 0), grad(hash, 0, 1, 0), grad(hash, 0, 0, 1)};
    };
    // Corners: 0=(0,0,0) 1=(1,0,0) 2=(0,1,0) 3=(1,1,0) 4=(0,0,1) 5=(1,0,1) 6=(0,1,1) 7=(1,1,1)
    const Vec3d g0 = gvec(P(aa)), g1 = gvec(P(ba)), g2 = gvec(P(ab)), g3 = gvec(P(bb));
    const Vec3d g4 = gvec(P(aa + 1)), g5 = gvec(P(ba + 1)), g6 = gvec(P(ab + 1)), g7 = gvec(P(bb + 1));
    const double v0 = dot(g0, Vec3d{x, y, z}), v1 = dot(g1, Vec3d{x - 1, y, z});
    const double v2 = dot(g2, Vec3d{x, y - 1, z}), v3 = dot(g3, Vec3d{x - 1, y - 1, z});
    const double v4 = dot(g4, Vec3d{x, y, z - 1}), v5 = dot(g5, Vec3d{x - 1, y, z - 1});
    const double v6 = dot(g6, Vec3d{x, y - 1, z - 1}), v7 = dot(g7, Vec3d{x - 1, y - 1, z - 1});

    // Trilinear interpolation written as a polynomial in (u, v, w)...
    const double k1 = v1 - v0, k2 = v2 - v0, k3 = v4 - v0;
    const double k4 = v0 - v1 - v2 + v3, k5 = v0 - v2 - v4 + v6, k6 = v0 - v1 - v4 + v5;
    const double k7 = -v0 + v1 + v2 - v3 + v4 - v5 - v6 + v7;
    const double value = v0 + u * k1 + v * k2 + w * k3 + u * v * k4 + v * w * k5 + w * u * k6 +
                         u * v * w * k7;

    // ...so its derivative is: interpolated corner gradients + fade derivative * polynomial slope.
    const Vec3d gk1 = g1 - g0, gk2 = g2 - g0, gk3 = g4 - g0;
    const Vec3d gk4 = g0 - g1 - g2 + g3, gk5 = g0 - g2 - g4 + g6, gk6 = g0 - g1 - g4 + g5;
    const Vec3d gk7 = g4 - g0 - g5 - g6 + g7 + g1 + g2 - g3; // = -g0+g1+g2-g3+g4-g5-g6+g7
    const Vec3d interpolated = g0 + gk1 * u + gk2 * v + gk3 * w + gk4 * (u * v) +
                               gk5 * (v * w) + gk6 * (w * u) + gk7 * (u * v * w);
    const Vec3d fadeTerm{du * (k1 + v * k4 + w * k6 + v * w * k7),
                         dv * (k2 + w * k5 + u * k4 + w * u * k7),
                         dw * (k3 + u * k6 + v * k5 + u * v * k7)};
    return {value, interpolated + fadeTerm};
}

double erodedFbm(const PerlinNoise& noise, const Vec3d& dir, double base, const FbmParams& params,
                 double erosion, double maxFrequency) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double totalAmplitude = 0.0;
    Vec3d slope{}; // accumulated tangential gradient
    for (int i = 0; i < params.octaves; ++i) {
        totalAmplitude += amplitude;
        const double fadeWeight = std::clamp(2.0 - 2.0 * frequency / maxFrequency, 0.0, 1.0);
        if (fadeWeight > 0.0) {
            const NoiseSample n = noise.sampleWithGradient(dir * (base * frequency));
            const Vec3d tangential = n.gradient - dir * dot(n.gradient, dir);
            slope += tangential;
            sum += fadeWeight * amplitude * n.value / (1.0 + erosion * lengthSquared(slope));
        }
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return totalAmplitude > 0.0 ? sum / totalAmplitude : 0.0;
}

double fbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params,
           double maxFrequency) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double totalAmplitude = 0.0;
    for (int i = 0; i < params.octaves; ++i) {
        // Fade, not cut: octaves appear gradually while zooming in.
        const double weight = std::clamp(2.0 - 2.0 * frequency / maxFrequency, 0.0, 1.0);
        if (weight > 0.0) {
            sum += weight * amplitude * noise.sample(p * frequency);
        }
        totalAmplitude += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return totalAmplitude > 0.0 ? sum / totalAmplitude : 0.0;
}

double ridgedFbm(const PerlinNoise& noise, const Vec3d& p, const FbmParams& params,
                 double maxFrequency) {
    double sum = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0;
    double totalAmplitude = 0.0;
    double weight = 1.0;
    for (int i = 0; i < params.octaves; ++i) {
        const double fade = std::clamp(2.0 - 2.0 * frequency / maxFrequency, 0.0, 1.0);
        if (fade <= 0.0) {
            // Frequencies only grow, so every remaining octave is filtered out too. Still count
            // their amplitude so the normalisation is identical at every level of detail.
            for (; i < params.octaves; ++i) {
                totalAmplitude += amplitude;
                amplitude *= params.gain;
            }
            break;
        }
        // |n| with a rounded tip: sqrt(n^2 + e^2) - e equals |n| away from zero but is smooth
        // at zero, so the crest has no kink. Clamped so the result stays in [0, 1].
        const double n = noise.sample(p * frequency);
        const double e = params.crestSoftness;
        const double absN = e > 0.0 ? std::sqrt(n * n + e * e) - e : std::abs(n);
        double signal = std::clamp(1.0 - absN, 0.0, 1.0);
        signal *= signal * weight;
        // Musgrave's weight clamp(2s, 0, 1) has a kink where it saturates - another family of
        // closed lines in the shading. In soft mode use smoothstep(0, 0.5, s): same shape, C1.
        if (e > 0.0) {
            const double t = std::clamp(signal * 2.0, 0.0, 1.0);
            weight = t * t * (3.0 - 2.0 * t);
        } else {
            weight = std::clamp(signal * 2.0, 0.0, 1.0);
        }

        sum += fade * amplitude * signal;
        totalAmplitude += amplitude;
        amplitude *= params.gain;
        frequency *= params.lacunarity;
    }
    return totalAmplitude > 0.0 ? sum / totalAmplitude : 0.0;
}

} // namespace orbis
