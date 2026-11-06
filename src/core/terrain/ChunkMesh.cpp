#include "core/terrain/ChunkMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace orbis {

namespace {

void validate(int quadsPerEdge) {
    if (quadsPerEdge < 2 || quadsPerEdge > kMaxQuadsPerEdge || quadsPerEdge % 2 != 0) {
        throw std::invalid_argument("chunk: quadsPerEdge must be even and in [2, 250]");
    }
}

/// Everything known about one surface point.
struct PointState {
    Vec3d position;
    Vec3d normal;
    double elevation = 0.0;
};

PointState average(const PointState& a, const PointState& b) {
    return {(a.position + b.position) * 0.5, normalize(a.normal + b.normal),
            (a.elevation + b.elevation) * 0.5};
}

struct SurfaceSample {
    Vec3d position;   ///< ECEF, metres.
    Vec3d up;         ///< Ellipsoid normal (geodetic vertical).
    double elevation; ///< Raw height-field value (may be negative).
};

/// Sea-level point along the direction, raised along the geodetic vertical. Oceans are flat
/// (max(elevation, 0)); the raw elevation is kept for shading.
SurfaceSample sampleSurface(const Vec3d& unitDir, double spacing, const Ellipsoid& ellipsoid,
                            const ElevationSource& terrain) {
    const double elevation = terrain.elevation(unitDir, spacing);
    const Vec3d seaLevel = ellipsoid.surfacePointAlong(unitDir);
    const Vec3d up = ellipsoid.surfaceNormal(seaLevel);
    return {seaLevel + up * std::max(elevation, 0.0), up, elevation};
}

/// Central differences around `unitDir` rather than between grid neighbours: the result depends
/// only on the direction, so chunks sharing a border vertex get bit-identical normals.
Vec3d surfaceNormalAt(const Vec3d& unitDir, double angularStep, double spacing,
                      const Ellipsoid& ellipsoid, const ElevationSource& terrain) {
    // cross(t1, t2) == unitDir, so the result points outwards.
    const Vec3d helper = std::abs(unitDir.z) < 0.9 ? Vec3d{0, 0, 1} : Vec3d{1, 0, 0};
    const Vec3d t1 = normalize(cross(helper, unitDir));
    const Vec3d t2 = cross(unitDir, t1);

    const auto at = [&](const Vec3d& offset) {
        return sampleSurface(normalize(unitDir + offset), spacing, ellipsoid, terrain).position;
    };
    const Vec3d d1 = at(t1 * angularStep) - at(t1 * -angularStep);
    const Vec3d d2 = at(t2 * angularStep) - at(t2 * -angularStep);
    return normalize(cross(d1, d2));
}

} // namespace

std::uint32_t packNormal(const Vec3d& n) {
    const auto component = [](double c) {
        const long q = std::lround(std::clamp(c, -1.0, 1.0) * 511.0);
        return static_cast<std::uint32_t>(q) & 0x3FFu; // two's complement, 10 bits
    };
    return component(n.x) | (component(n.y) << 10) | (component(n.z) << 20);
}

Vec3f unpackNormal(std::uint32_t packed) {
    const auto component = [packed](int shift) {
        int v = static_cast<int>((packed >> shift) & 0x3FFu);
        if (v >= 512) {
            v -= 1024; // sign-extend
        }
        return std::max(static_cast<float>(v) / 511.0f, -1.0f); // same rule as OpenGL
    };
    return {component(0), component(10), component(20)};
}

double chunkVertexSpacing(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid) {
    // ~pi/4 rad per unit of u.
    return region.size / quadsPerEdge * (std::numbers::pi / 4.0) * ellipsoid.semiMajorAxis();
}

double chunkSkirtDepth(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid) {
    // A crack is bounded by the relief and curvature sag over one coarse cell: one cell size
    // covers 45 degree slopes, capped for the huge cells of coarse levels.
    const double spacing = chunkVertexSpacing(region, quadsPerEdge, ellipsoid);
    return std::min(spacing, kMaxTerrainElevation + 3000.0);
}

int boundaryGridIndex(int k, int n) {
    const int stride = n + 1;
    if (k < n) {
        return k;                          // bottom edge, j = 0, i = 0 .. n-1
    }
    if (k < 2 * n) {
        return (k - n) * stride + n;       // right edge, i = n, j = 0 .. n-1
    }
    if (k < 3 * n) {
        return n * stride + (3 * n - k);   // top edge, j = n, i = n .. 1
    }
    return (4 * n - k) * stride;           // left edge, i = 0, j = n .. 1
}

ChunkMesh buildChunkMesh(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid,
                         const ElevationSource& terrain) {
    validate(quadsPerEdge);

    const int n = quadsPerEdge;
    const int half = n / 2;
    const double step = region.size / n;
    const double spacing = chunkVertexSpacing(region, n, ellipsoid);
    const double angularStep = 0.5 * step * (std::numbers::pi / 4.0);
    const bool hasParent = region.size < 2.0;

    ChunkMesh mesh;
    mesh.region = region;

    const double uCenter = region.uMin + region.size / 2.0;
    const double vCenter = region.vMin + region.size / 2.0;
    mesh.origin = ellipsoid.surfacePointAlong(faceToSphere(region.face, uCenter, vCenter));

    // Exact border coordinates: neighbours and the parent sample shared points bit-identically.
    const auto coordinate = [&](double min, int index, int count, double delta) {
        return index == count ? min + region.size : min + index * delta;
    };

    // The parent's surface, sampled exactly as the parent's own mesh does.
    std::vector<PointState> parent;
    if (hasParent) {
        parent.reserve(static_cast<std::size_t>((half + 1) * (half + 1)));
        for (int j = 0; j <= half; ++j) {
            const double v = coordinate(region.vMin, j, half, 2.0 * step);
            for (int i = 0; i <= half; ++i) {
                const double u = coordinate(region.uMin, i, half, 2.0 * step);
                const Vec3d dir = faceToSphere(region.face, u, v);
                const SurfaceSample s = sampleSurface(dir, 2.0 * spacing, ellipsoid, terrain);
                parent.push_back({s.position,
                                  surfaceNormalAt(dir, 2.0 * angularStep, 2.0 * spacing, ellipsoid,
                                                  terrain),
                                  s.elevation});
            }
        }
    }
    const auto parentAt = [&](int i, int j) -> const PointState& {
        return parent[static_cast<std::size_t>(j * (half + 1) + i)];
    };

    mesh.vertices.reserve(static_cast<std::size_t>(chunkVertexCount(n)));
    std::vector<Vec3d> ups; // needed again for the skirt
    ups.reserve(static_cast<std::size_t>(chunkGridVertexCount(n)));
    double maxDistanceSq = 0.0;
    mesh.minHeight = std::numeric_limits<double>::infinity();
    mesh.maxHeight = -std::numeric_limits<double>::infinity();
    const auto trackHeight = [&](const Vec3d& p) {
        const double h = std::max(ellipsoid.ecefToGeodetic(p).height, 0.0);
        mesh.minHeight = std::min(mesh.minHeight, h);
        mesh.maxHeight = std::max(mesh.maxHeight, h);
    };

    for (int j = 0; j <= n; ++j) {
        const double v = coordinate(region.vMin, j, n, step);
        for (int i = 0; i <= n; ++i) {
            const double u = coordinate(region.uMin, i, n, step);

            const Vec3d dir = faceToSphere(region.face, u, v);
            const SurfaceSample s = sampleSurface(dir, spacing, ellipsoid, terrain);
            const PointState own{s.position,
                                 surfaceNormalAt(dir, angularStep, spacing, ellipsoid, terrain),
                                 s.elevation};

            // Morph target on the parent's triangles: even/even vertices are parent vertices,
            // the others interpolate along a parent edge or the (i, j) -> (i+1, j+1) diagonal
            // used by buildChunkIndices().
            PointState target = own;
            if (hasParent) {
                const int pi = i / 2;
                const int pj = j / 2;
                const bool oddI = (i % 2) != 0;
                const bool oddJ = (j % 2) != 0;
                if (!oddI && !oddJ) {
                    target = parentAt(pi, pj);
                } else if (oddI && !oddJ) {
                    target = average(parentAt(pi, pj), parentAt(pi + 1, pj));
                } else if (!oddI && oddJ) {
                    target = average(parentAt(pi, pj), parentAt(pi, pj + 1));
                } else {
                    target = average(parentAt(pi, pj), parentAt(pi + 1, pj + 1));
                }
            }

            const Vec3d local = own.position - mesh.origin; // subtract in DOUBLE, then narrow
            maxDistanceSq = std::max(maxDistanceSq, lengthSquared(local));
            trackHeight(own.position);
            trackHeight(target.position);

            mesh.vertices.push_back({
                .position = Vec3f(local),
                .morphDelta = Vec3f(target.position - own.position),
                .normal = packNormal(own.normal),
                .morphNormal = packNormal(target.normal),
                .elevation = static_cast<float>(own.elevation),
                .morphElevation = static_cast<float>(target.elevation),
            });
            ups.push_back(s.up);
        }
    }

    // Border vertices pushed down; same normal and morph so they move and shade with the edge.
    const double depth = chunkSkirtDepth(region, n, ellipsoid);
    for (int k = 0; k < chunkSkirtVertexCount(n); ++k) {
        const auto g = static_cast<std::size_t>(boundaryGridIndex(k, n));
        TerrainVertex skirt = mesh.vertices[g];
        const Vec3d top = mesh.origin + Vec3d(skirt.position);
        skirt.position = Vec3f(top - ups[g] * depth - mesh.origin);
        mesh.vertices.push_back(skirt);
    }

    mesh.boundingRadius = std::sqrt(maxDistanceSq);
    return mesh;
}

std::vector<std::uint16_t> buildChunkIndices(int quadsPerEdge) {
    validate(quadsPerEdge);

    const int n = quadsPerEdge;
    const int stride = n + 1;
    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(chunkGridIndexCount(n) + chunkSkirtIndexCount(n)));
    const auto push = [&indices](int a, int b, int c) {
        indices.push_back(static_cast<std::uint16_t>(a));
        indices.push_back(static_cast<std::uint16_t>(b));
        indices.push_back(static_cast<std::uint16_t>(c));
    };

    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            //  d --- c      a = (i, j)      b = (i+1, j)
            //  |   / |      c = (i+1, j+1)  d = (i, j+1)
            //  | /   |      (a, b, c) and (a, c, d) are counter-clockwise when u x v
            //  a --- b      points outwards, which FaceBasis guarantees.
            const int a = j * stride + i;
            const int b = a + 1;
            const int c = a + stride + 1;
            const int d = a + stride;
            push(a, b, c);
            push(a, c, d);
        }
    }

    // The border runs counter-clockwise, so this winding faces the skirt outwards.
    const int skirtBase = chunkGridVertexCount(n);
    const int ring = chunkSkirtVertexCount(n);
    for (int k = 0; k < ring; ++k) {
        const int next = (k + 1) % ring;
        const int e0 = boundaryGridIndex(k, n);
        const int e1 = boundaryGridIndex(next, n);
        const int s0 = skirtBase + k;
        const int s1 = skirtBase + next;
        push(e0, s0, e1);
        push(e1, s0, s1);
    }
    return indices;
}

} // namespace orbis
