#include "core/terrain/ChunkMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace orbis {

namespace {

struct SurfaceSample {
    Vec3d position;   ///< ECEF, metres.
    double elevation; ///< Raw height-field value (may be negative).
};

/// Terrain point for a direction from the planet centre.
///   1. hit the ellipsoid along the direction  -> sea-level point
///   2. push it out along the ellipsoid normal  -> geodetic "up", same as a real altimeter
/// Oceans are flat: geometry uses max(elevation, 0); the raw value is kept for colouring.
SurfaceSample sampleSurface(const Vec3d& unitDir, const Ellipsoid& ellipsoid,
                            const HeightField& heightField) {
    const double elevation = heightField.elevation(unitDir);
    const Vec3d seaLevel = ellipsoid.surfacePointAlong(unitDir);
    const Vec3d up = ellipsoid.surfaceNormal(seaLevel);
    return {seaLevel + up * std::max(elevation, 0.0), elevation};
}

/// Surface normal by central differences around `unitDir`, independent of cube faces.
///
/// Why not differences between neighbouring grid vertices? At a chunk or face border the
/// neighbours belong to ANOTHER chunk, so each side would compute a different normal and the
/// lighting would show a visible seam. Here the normal depends only on the direction itself, so
/// two chunks sharing a border vertex compute bit-identical normals.
Vec3d surfaceNormalAt(const Vec3d& unitDir, double angularStep, const Ellipsoid& ellipsoid,
                      const HeightField& heightField) {
    // Any tangent basis works; it is derived from unitDir alone so it is deterministic.
    // cross(t1, t2) == unitDir, so the result points outwards.
    const Vec3d helper = std::abs(unitDir.z) < 0.9 ? Vec3d{0, 0, 1} : Vec3d{1, 0, 0};
    const Vec3d t1 = normalize(cross(helper, unitDir));
    const Vec3d t2 = cross(unitDir, t1);

    const auto at = [&](const Vec3d& offset) {
        return sampleSurface(normalize(unitDir + offset), ellipsoid, heightField).position;
    };
    const Vec3d d1 = at(t1 * angularStep) - at(t1 * -angularStep);
    const Vec3d d2 = at(t2 * angularStep) - at(t2 * -angularStep);
    return normalize(cross(d1, d2));
}

} // namespace

ChunkMesh buildChunkMesh(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid,
                         const HeightField& heightField) {
    if (quadsPerEdge < 1 || quadsPerEdge > kMaxQuadsPerEdge) {
        throw std::invalid_argument("buildChunkMesh: quadsPerEdge out of range");
    }

    const int verticesPerEdge = quadsPerEdge + 1;
    const double step = region.size / quadsPerEdge;
    // A face spans ~90 degrees over 2 units of u, so one unit of u is ~pi/4 radians.
    const double angularStep = 0.5 * step * 0.785398;

    ChunkMesh mesh;
    mesh.region = region;

    // Anchor at the sea-level point under the region centre. Any point near the chunk works;
    // what matters is that every vertex is CLOSE to it.
    const double uCenter = region.uMin + region.size / 2.0;
    const double vCenter = region.vMin + region.size / 2.0;
    mesh.origin = ellipsoid.surfacePointAlong(faceToSphere(region.face, uCenter, vCenter));

    mesh.vertices.reserve(static_cast<std::size_t>(verticesPerEdge * verticesPerEdge));
    double maxDistanceSq = 0.0;

    // Row-major, u varies fastest: index = j * verticesPerEdge + i.
    for (int j = 0; j < verticesPerEdge; ++j) {
        // Exact endpoints on the borders (no accumulated rounding), so neighbouring chunks
        // produce the same u/v - and therefore the same vertices - on their shared edge.
        const double v = (j == quadsPerEdge) ? region.vMin + region.size : region.vMin + j * step;
        for (int i = 0; i < verticesPerEdge; ++i) {
            const double u =
                (i == quadsPerEdge) ? region.uMin + region.size : region.uMin + i * step;

            const Vec3d dir = faceToSphere(region.face, u, v);
            const SurfaceSample s = sampleSurface(dir, ellipsoid, heightField);
            const Vec3d normal = surfaceNormalAt(dir, angularStep, ellipsoid, heightField);

            const Vec3d local = s.position - mesh.origin; // subtract in DOUBLE, then narrow
            maxDistanceSq = std::max(maxDistanceSq, lengthSquared(local));

            mesh.vertices.push_back({
                .position = Vec3f(local),
                .normal = Vec3f(normal),
                .elevation = static_cast<float>(s.elevation),
            });
        }
    }

    mesh.boundingRadius = std::sqrt(maxDistanceSq);
    return mesh;
}

std::vector<std::uint16_t> buildGridIndices(int quadsPerEdge) {
    if (quadsPerEdge < 1 || quadsPerEdge > kMaxQuadsPerEdge) {
        throw std::invalid_argument("buildGridIndices: quadsPerEdge out of range");
    }

    const int stride = quadsPerEdge + 1;
    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(quadsPerEdge * quadsPerEdge * 6));

    const auto push = [&indices](int index) { indices.push_back(static_cast<std::uint16_t>(index)); };

    for (int j = 0; j < quadsPerEdge; ++j) {
        for (int i = 0; i < quadsPerEdge; ++i) {
            //  d --- c      a = (i, j)      b = (i+1, j)
            //  |   / |      c = (i+1, j+1)  d = (i, j+1)
            //  | /   |      (a, b, c) and (a, c, d) are counter-clockwise when u x v
            //  a --- b      points outwards, which FaceBasis guarantees.
            const int a = j * stride + i;
            const int b = a + 1;
            const int c = a + stride + 1;
            const int d = a + stride;
            push(a); push(b); push(c);
            push(a); push(c); push(d);
        }
    }
    return indices;
}

} // namespace orbis
