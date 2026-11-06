#include "core/atmosphere/Clouds.hpp"

#include "core/terrain/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace orbis {

namespace {

/// (near, far) of a ray-sphere intersection, or nullopt-like (false) on a miss.
bool raySphere(const Vec3d& o, const Vec3d& d, double radius, double& tNear, double& tFar) {
    const double b = dot(o, d);
    const double c = dot(o, o) - radius * radius;
    const double disc = b * b - c;
    if (disc < 0.0) {
        return false;
    }
    const double s = std::sqrt(disc);
    tNear = -b - s;
    tFar = -b + s;
    return true;
}

double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

ShellIntervals rayShellIntervals(const Vec3d& origin, const Vec3d& dir, double inner, double outer,
                                 double ground) {
    ShellIntervals out;
    double outerNear = 0.0;
    double outerFar = 0.0;
    if (!raySphere(origin, dir, outer, outerNear, outerFar) || outerFar <= 0.0) {
        return out; // misses the shell, or it is entirely behind
    }
    // Where the ray stops: at the ground, if it hits it in front of the origin.
    double stop = outerFar;
    double groundNear = 0.0;
    double groundFar = 0.0;
    if (raySphere(origin, dir, ground, groundNear, groundFar) && groundNear > 0.0) {
        stop = std::min(stop, groundNear);
    }

    const auto add = [&out](double a, double b) {
        a = std::max(a, 0.0);
        if (b > a && out.count < 2) {
            out.t0[out.count] = a;
            out.t1[out.count] = b;
            ++out.count;
        }
    };
    double innerNear = 0.0;
    double innerFar = 0.0;
    if (!raySphere(origin, dir, inner, innerNear, innerFar)) {
        add(outerNear, std::min(outerFar, stop)); // passes above the inner sphere: one chord
    } else {
        // Inside the shell = inside outer AND outside inner: [outerNear, innerNear] and
        // [innerFar, outerFar], each clipped to where the ray stops.
        add(outerNear, std::min(innerNear, stop));
        add(innerFar, std::min(outerFar, stop));
    }
    return out;
}

Vec3d cubeMapDirection(int face, double s, double t) {
    const double sc = 2.0 * s - 1.0;
    const double tc = 2.0 * t - 1.0;
    // Table 8.19 of the OpenGL 4.5 spec, inverted: major axis ma = 1, sc / tc as listed.
    switch (face) {
    case 0: return {1.0, -tc, -sc};  // +X: sc = -rz, tc = -ry
    case 1: return {-1.0, -tc, sc};  // -X: sc = +rz, tc = -ry
    case 2: return {sc, 1.0, tc};    // +Y: sc = +rx, tc = +rz
    case 3: return {sc, -1.0, -tc};  // -Y: sc = +rx, tc = -rz
    case 4: return {sc, -tc, 1.0};   // +Z: sc = +rx, tc = -ry
    case 5: return {-sc, -tc, -1.0}; // -Z: sc = -rx, tc = -ry
    default: throw std::invalid_argument("cubeMapDirection: face must be 0..5");
    }
}

void cubeMapLookup(const Vec3d& d, int& face, double& s, double& t) {
    const double ax = std::abs(d.x);
    const double ay = std::abs(d.y);
    const double az = std::abs(d.z);
    double sc = 0.0;
    double tc = 0.0;
    double ma = 0.0;
    if (ax >= ay && ax >= az) {
        face = d.x > 0.0 ? 0 : 1;
        ma = ax;
        sc = d.x > 0.0 ? -d.z : d.z;
        tc = -d.y;
    } else if (ay >= az) {
        face = d.y > 0.0 ? 2 : 3;
        ma = ay;
        sc = d.x;
        tc = d.y > 0.0 ? d.z : -d.z;
    } else {
        face = d.z > 0.0 ? 4 : 5;
        ma = az;
        sc = d.z > 0.0 ? d.x : -d.x;
        tc = -d.y;
    }
    s = 0.5 * (sc / ma + 1.0);
    t = 0.5 * (tc / ma + 1.0);
}

double cloudClimatology(double latitude) {
    const double deg = std::abs(latitude) * 180.0 / std::numbers::pi;
    // Fitted to the shape of satellite cloud-fraction climatologies: a peak at the tropical
    // convergence zone (~5 deg), a minimum over the subtropical highs (~25 deg), the cloudiest
    // belt along the mid-latitude storm tracks (~55-60 deg), drier polar caps.
    const double itcz = 0.15 * std::exp(-std::pow((deg - 5.0) / 7.0, 2.0));
    const double subtropics = -0.25 * std::exp(-std::pow((deg - 25.0) / 9.0, 2.0));
    const double stormTracks = 0.20 * std::exp(-std::pow((deg - 57.0) / 12.0, 2.0));
    const double polar = -0.10 * smoothstep(70.0, 90.0, deg);
    return itcz + subtropics + stormTracks + polar;
}

std::vector<float> makeCloudWeather(int faceSize, std::uint64_t seed) {
    if (faceSize < 4) {
        throw std::invalid_argument("makeCloudWeather: faceSize too small");
    }
    const PerlinNoise noise(seed);
    const PerlinNoise warp(seed ^ 0x9E3779B97F4A7C15ull);
    const auto faceTexels = static_cast<std::size_t>(faceSize) * static_cast<std::size_t>(faceSize);
    std::vector<float> out(faceTexels * 6);
    // One thread per face; each writes only its own slice.
    const auto fillFace = [&](int face) {
        std::size_t i = static_cast<std::size_t>(face) * faceTexels;
        for (int y = 0; y < faceSize; ++y) {
            for (int x = 0; x < faceSize; ++x) {
                const Vec3d dir = normalize(cubeMapDirection(face, (x + 0.5) / faceSize, (y + 0.5) / faceSize));
                // Domain warp: weather systems are swirls and fronts, not round blobs.
                const Vec3d w{fbm(warp, dir * 3.0, {.octaves = 3}), fbm(warp, dir * 3.0 + Vec3d{5.2, 1.3, 7.1}, {.octaves = 3}),
                              fbm(warp, dir * 3.0 + Vec3d{2.8, 9.4, 3.3}, {.octaves = 3})};
                // Base frequency 2 on the unit sphere ~ 3000 km systems; 7 octaves reach ~30 km.
                const double n = fbm(noise, dir * 2.0 + w * 0.6, {.octaves = 7, .lacunarity = 2.0, .gain = 0.55});
                const double latitude = std::asin(std::clamp(dir.z, -1.0, 1.0));
                const double value = 0.5 + 1.1 * n + cloudClimatology(latitude);
                out[i++] = static_cast<float>(std::clamp(value, 0.0, 1.0));
            }
        }
    };
    {
        std::vector<std::jthread> threads;
        for (int face = 0; face < 6; ++face) {
            threads.emplace_back(fillFace, face);
        }
    }
    return out;
}

std::string toGlsl(const CloudParameters& p) {
    const auto divides = [](double small, double big) {
        const double ratio = big / small;
        return std::abs(ratio - std::round(ratio)) < 1e-9;
    };
    if (!divides(p.detailTile, p.shapeTile) || !divides(p.shapeTile, p.regionalTile)) {
        throw std::invalid_argument("CloudParameters: each noise tile must divide the next larger one");
    }
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer),
                  "// Generated from CloudParameters (src/core/atmosphere/Clouds.hpp). Do not edit.\n"
                  "const float kCloudBottom = %.9g;\n"
                  "const float kCloudTop = %.9g;\n"
                  "const float kCloudExtinction = %.9g;\n"
                  "const float kCloudRegionalTile = %.9g;\n"
                  "const float kCloudShapeTile = %.9g;\n"
                  "const float kCloudDetailTile = %.9g;\n"
                  "const float kCloudPhaseForward = %.9g;\n"
                  "const float kCloudPhaseBackward = %.9g;\n"
                  "const float kCloudPhaseBlend = %.9g;\n",
                  p.bottomAltitude, p.topAltitude, p.extinction, p.regionalTile, p.shapeTile,
                  p.detailTile,
                  p.phaseForward, p.phaseBackward, p.phaseBlend);
    return buffer;
}

} // namespace orbis
