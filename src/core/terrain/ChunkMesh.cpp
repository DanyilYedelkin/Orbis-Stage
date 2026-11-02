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
    if (quadsPerEdge < 1 || quadsPerEdge > kMaxQuadsPerEdge) {
        throw std::invalid_argument("chunk: quadsPerEdge out of range");
    }
}

struct SurfaceSample {
    Vec3d position;   ///< ECEF, metres.
    Vec3d up;         ///< Ellipsoid normal (geodetic vertical).
    double elevation; ///< Raw height-field value (may be negative).
};

/// Terrain point for a direction from the planet centre.
///   1. hit the ellipsoid along the direction  -> sea-level point
///   2. push it out along the ellipsoid normal  -> geodetic "up", same as a real altimeter
/// Oceans are flat: geometry uses max(elevation, 0); the raw value is kept for colouring.
SurfaceSample sampleSurface(const Vec3d& unitDir, double spacing, const Ellipsoid& ellipsoid,
                            const HeightField& heightField) {
    const double elevation = heightField.elevation(unitDir, spacing);
    const Vec3d seaLevel = ellipsoid.surfacePointAlong(unitDir);
    const Vec3d up = ellipsoid.surfaceNormal(seaLevel);
    return {seaLevel + up * std::max(elevation, 0.0), up, elevation};
}

/// Surface normal by central differences around `unitDir`, independent of cube faces.
///
/// Why not differences between neighbouring grid vertices? At a chunk or face border the
/// neighbours belong to ANOTHER chunk, so each side would compute a different normal and the
/// lighting would show a visible seam. Here the normal depends only on the direction itself, so
/// two chunks sharing a border vertex compute bit-identical normals.
Vec3d surfaceNormalAt(const Vec3d& unitDir, double angularStep, double spacing,
                      const Ellipsoid& ellipsoid, const HeightField& heightField) {
    // Any tangent basis works; it is derived from unitDir alone so it is deterministic.
    // cross(t1, t2) == unitDir, so the result points outwards.
    const Vec3d helper = std::abs(unitDir.z) < 0.9 ? Vec3d{0, 0, 1} : Vec3d{1, 0, 0};
    const Vec3d t1 = normalize(cross(helper, unitDir));
    const Vec3d t2 = cross(unitDir, t1);

    const auto at = [&](const Vec3d& offset) {
        return sampleSurface(normalize(unitDir + offset), spacing, ellipsoid, heightField).position;
    };
    const Vec3d d1 = at(t1 * angularStep) - at(t1 * -angularStep);
    const Vec3d d2 = at(t2 * angularStep) - at(t2 * -angularStep);
    return normalize(cross(d1, d2));
}

} // namespace

double chunkVertexSpacing(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid) {
    // A face spans ~90 degrees (pi/2 rad) over 2 units of u, i.e. ~pi/4 rad per unit.
    return region.size / quadsPerEdge * (std::numbers::pi / 4.0) * ellipsoid.semiMajorAxis();
}

double chunkSkirtDepth(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid) {
    // A crack is at most what the coarser neighbour misses between two of its vertices:
    // terrain relief over one (coarse) cell plus the curvature sag of that cell. One cell size
    // covers slopes up to 45 degrees at fine levels; at coarse levels the cell size is huge, so
    // cap it at the maximum terrain height plus the sag of a root cell (~2 km).
    const double spacing = chunkVertexSpacing(region, quadsPerEdge, ellipsoid);
    return std::min(spacing, HeightField::maxElevation() + 3000.0);
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
                         const HeightField& heightField) {
    validate(quadsPerEdge);

    const int n = quadsPerEdge;
    const int verticesPerEdge = n + 1;
    const double step = region.size / n;
    const double spacing = chunkVertexSpacing(region, n, ellipsoid);
    const double angularStep = 0.5 * step * (std::numbers::pi / 4.0);

    ChunkMesh mesh;
    mesh.region = region;

    // Anchor at the sea-level point under the region centre. Any point near the chunk works;
    // what matters is that every vertex is CLOSE to it.
    const double uCenter = region.uMin + region.size / 2.0;
    const double vCenter = region.vMin + region.size / 2.0;
    mesh.origin = ellipsoid.surfacePointAlong(faceToSphere(region.face, uCenter, vCenter));

    mesh.vertices.reserve(static_cast<std::size_t>(chunkVertexCount(n)));
    std::vector<Vec3d> ups; // needed again for the skirt
    ups.reserve(static_cast<std::size_t>(chunkGridVertexCount(n)));
    double maxDistanceSq = 0.0;
    mesh.minHeight = std::numeric_limits<double>::infinity();
    mesh.maxHeight = -std::numeric_limits<double>::infinity();

    for (int j = 0; j < verticesPerEdge; ++j) {
        // Exact endpoints on the borders (no accumulated rounding), so neighbouring chunks
        // produce the same u/v - and therefore the same vertices - on their shared edge.
        const double v = (j == n) ? region.vMin + region.size : region.vMin + j * step;
        for (int i = 0; i < verticesPerEdge; ++i) {
            const double u = (i == n) ? region.uMin + region.size : region.uMin + i * step;

            const Vec3d dir = faceToSphere(region.face, u, v);
            const SurfaceSample s = sampleSurface(dir, spacing, ellipsoid, heightField);
            const Vec3d normal = surfaceNormalAt(dir, angularStep, spacing, ellipsoid, heightField);

            const Vec3d local = s.position - mesh.origin; // subtract in DOUBLE, then narrow
            maxDistanceSq = std::max(maxDistanceSq, lengthSquared(local));
            const double height = std::max(s.elevation, 0.0);
            mesh.minHeight = std::min(mesh.minHeight, height);
            mesh.maxHeight = std::max(mesh.maxHeight, height);

            mesh.vertices.push_back({
                .position = Vec3f(local),
                .normal = Vec3f(normal),
                .elevation = static_cast<float>(s.elevation),
            });
            ups.push_back(s.up);
        }
    }

    // Skirt: a copy of every border vertex, pushed straight down. Same normal and elevation as
    // the border vertex, so if a crack exposes it, it is shaded like the terrain next to it.
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

    // Skirt quads between border vertex e_k and the skirt vertex s_k below it. The border is
    // walked counter-clockwise, so "outside the chunk" is on the right of the walking direction;
    // this winding makes the skirt face outwards - towards the neighbour whose crack it hides.
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
