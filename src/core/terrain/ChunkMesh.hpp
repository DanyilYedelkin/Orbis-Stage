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

/// GPU vertex. Carries its own state and the parent's surface at the same point; the vertex
/// shader morphs between them by distance so LOD switches do not pop.
struct TerrainVertex {
    Vec3f position;            ///< Relative to ChunkMesh::origin.
    Vec3f morphDelta;          ///< Parent position minus `position`.
    std::uint32_t normal;      ///< ECEF, packNormal().
    std::uint32_t morphNormal;
    float elevation;           ///< Unclamped, for shading.
    float morphElevation;
};
static_assert(sizeof(TerrainVertex) == 40, "unexpected padding");
static_assert(std::is_standard_layout_v<TerrainVertex>, "offsetof() must be valid");

/// GL_INT_2_10_10_10_REV, normalised.
std::uint32_t packNormal(const Vec3d& n);
Vec3f unpackNormal(std::uint32_t packed);

/// Vertices: the (n+1)^2 grid, row-major with u fastest, then one skirt vertex per border
/// vertex in boundaryGridIndex() order.
struct ChunkMesh {
    FaceRegion region;
    Vec3d origin;                ///< ECEF.
    double boundingRadius = 0.0; ///< Surface vertices only.
    double minHeight = 0.0;      ///< Over own and morph positions, clamped at 0.
    double maxHeight = 0.0;
    std::uint64_t dataVersion = 0; ///< ElevationSource::version() when the build started.
    std::vector<TerrainVertex> vertices;
};

double chunkVertexSpacing(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid);

/// Must exceed the largest crack between LOD levels; deeper is harmless.
double chunkSkirtDepth(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid);

ChunkMesh buildChunkMesh(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid,
                         const ElevationSource& terrain);

/// Same for every chunk of a resolution: one shared index buffer.
std::vector<std::uint16_t> buildChunkIndices(int quadsPerEdge);

constexpr int chunkGridVertexCount(int n) { return (n + 1) * (n + 1); }
constexpr int chunkSkirtVertexCount(int n) { return 4 * n; }
constexpr int chunkVertexCount(int n) { return chunkGridVertexCount(n) + chunkSkirtVertexCount(n); }
constexpr int chunkGridIndexCount(int n) { return n * n * 6; }
constexpr int chunkSkirtIndexCount(int n) { return 4 * n * 6; }

/// k in [0, 4n), counter-clockwise seen from outside: bottom, right, top, left edge.
int boundaryGridIndex(int k, int quadsPerEdge);

/// quadsPerEdge must be even so every other child vertex is a parent vertex (geomorphing).
inline constexpr int kMaxQuadsPerEdge = 250;
static_assert(chunkVertexCount(kMaxQuadsPerEdge) <= 65536);

} // namespace orbis
