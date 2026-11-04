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

/// Exactly what the GPU receives per vertex: 40 bytes.
///
/// Every vertex carries TWO states for geomorphing (smooth LOD transitions): its own, and where
/// it would be on the PARENT's coarser surface. The vertex shader blends between them by
/// distance, so a chunk that just replaced its parent looks exactly like the parent and only
/// grows its own detail as the camera approaches - no "pop".
struct TerrainVertex {
    Vec3f position;            ///< Metres, relative to ChunkMesh::origin - SMALL numbers, float-safe.
    Vec3f morphDelta;          ///< Parent-surface position minus `position`.
    std::uint32_t normal;      ///< Unit normal (ECEF-aligned), packed snorm 10:10:10:2.
    std::uint32_t morphNormal; ///< Normal of the parent surface at this point, packed.
    float elevation;           ///< Metres, unclamped (negative = ocean floor), for colouring.
    float morphElevation;      ///< The parent's elevation at this point.
};
static_assert(sizeof(TerrainVertex) == 40, "unexpected padding");
static_assert(std::is_standard_layout_v<TerrainVertex>, "offsetof() must be valid");

/// Unit vector -> GL_INT_2_10_10_10_REV (normalised). 10 bits per axis is ~0.1 degree: plenty for
/// shading, and 4 bytes instead of 12.
std::uint32_t packNormal(const Vec3d& n);
Vec3f unpackNormal(std::uint32_t packed);

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
    double minHeight = 0.0;      ///< Lowest surface point (own or morph target), metres, >= 0.
    double maxHeight = 0.0;      ///< Highest surface point (own or morph target).
    /// ElevationSource::version() when the build started: data that changed later (streamed
    /// tiles) is not in this mesh.
    std::uint64_t dataVersion = 0;
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

/// Must be EVEN: every second vertex of a child then coincides with a parent vertex, which is
/// what geomorphing relies on. 250 keeps grid + skirt within 16-bit indices.
inline constexpr int kMaxQuadsPerEdge = 250;
static_assert(chunkVertexCount(kMaxQuadsPerEdge) <= 65536);

} // namespace orbis
