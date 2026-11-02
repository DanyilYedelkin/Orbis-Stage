#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Vec3.hpp"
#include "core/terrain/CubeSphere.hpp"
#include "core/terrain/ElevationSource.hpp"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace orbis {

/// A square region of one cube face in face coordinates (u, v in [-1, 1]).
struct FaceRegion {
    CubeFace face = CubeFace::PosX;
    double uMin = -1.0;
    double vMin = -1.0;
    double size = 2.0;
};

/// Exactly what the GPU receives per vertex: 7 floats, 28 bytes.
struct TerrainVertex {
    Vec3f position;  ///< Metres, relative to ChunkMesh::origin - SMALL numbers, float-safe.
    Vec3f normal;    ///< Unit, ECEF-aligned.
    float elevation; ///< Metres, unclamped (negative = ocean floor), used for colouring.
};
static_assert(sizeof(TerrainVertex) == 7 * sizeof(float), "unexpected padding");
static_assert(std::is_standard_layout_v<TerrainVertex>, "offsetof() must be valid");

/// CPU-side mesh of one chunk. Pure data: no OpenGL here, so it is unit-testable and is built on
/// worker threads.
///
/// Vertex layout:
///   [0, (n+1)^2)              the surface grid, row-major, u fastest: index = j * (n+1) + i
///   [(n+1)^2, (n+1)^2 + 4n)   the SKIRT: one vertex below every border vertex, in the order of
///                             boundaryGridIndex(k)
struct ChunkMesh {
    FaceRegion region;
    Vec3d origin;                ///< ECEF anchor all vertex positions are relative to.
    double boundingRadius = 0.0; ///< Sphere around origin containing every SURFACE vertex.
    double minHeight = 0.0;      ///< Lowest surface vertex above the ellipsoid (geometry, >= 0).
    double maxHeight = 0.0;      ///< Highest surface vertex above the ellipsoid.
    std::vector<TerrainVertex> vertices;
};

/// Approximate distance between neighbouring vertices of a chunk, in metres.
double chunkVertexSpacing(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid);

/// How far skirts hang below the border. Must exceed the largest crack between neighbouring LOD
/// levels; too deep is harmless (skirts are hidden under the terrain).
double chunkSkirtDepth(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid);

ChunkMesh buildChunkMesh(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid,
                         const ElevationSource& terrain);

/// Triangle indices for grid + skirt. Identical for every chunk with the same resolution, so one
/// index buffer is shared by all chunks on the GPU. 16-bit indices halve its size.
std::vector<std::uint16_t> buildChunkIndices(int quadsPerEdge);

constexpr int chunkGridVertexCount(int n) { return (n + 1) * (n + 1); }
constexpr int chunkSkirtVertexCount(int n) { return 4 * n; }
constexpr int chunkVertexCount(int n) { return chunkGridVertexCount(n) + chunkSkirtVertexCount(n); }
constexpr int chunkGridIndexCount(int n) { return n * n * 6; }
constexpr int chunkSkirtIndexCount(int n) { return 4 * n * 6; }

/// Grid index of the k-th border vertex, k in [0, 4n). The border is walked counter-clockwise
/// as seen from outside the planet: bottom edge (+u), right edge (+v), top edge (-u), left (-v).
int boundaryGridIndex(int k, int quadsPerEdge);

inline constexpr int kMaxQuadsPerEdge = 250; ///< Keeps grid + skirt within 16-bit indices.
static_assert(chunkVertexCount(kMaxQuadsPerEdge) <= 65536);

} // namespace orbis
