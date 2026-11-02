#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Vec3.hpp"
#include "core/terrain/CubeSphere.hpp"
#include "core/terrain/HeightField.hpp"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace orbis {

/// A square region of one cube face in face coordinates (u, v in [-1, 1]).
/// Stage 1 tiles every face with a fixed grid of regions; stage 2 makes this a quadtree node.
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

/// CPU-side mesh of one chunk. Pure data: no OpenGL here, so it is unit-testable and can be
/// built on worker threads (stage 3).
struct ChunkMesh {
    FaceRegion region;
    Vec3d origin;                ///< ECEF anchor all vertex positions are relative to.
    double boundingRadius = 0.0; ///< Sphere around origin containing every vertex.
    std::vector<TerrainVertex> vertices;
};

/// Builds a (quadsPerEdge + 1)^2 vertex grid over `region`, displaced by the height field.
///
/// Every position is computed in double precision in ECEF, then stored RELATIVE to the
/// chunk origin as float. Absolute ECEF coordinates (~6.4e6 m) in float have a precision of
/// ~0.5 m, which shows up as shaking vertices; relative to a nearby origin it is sub-millimetre.
ChunkMesh buildChunkMesh(const FaceRegion& region, int quadsPerEdge, const Ellipsoid& ellipsoid,
                         const HeightField& heightField);

/// Triangle indices for that grid. Identical for every chunk with the same quadsPerEdge, so one
/// index buffer is shared by all chunks on the GPU. 16-bit indices halve its size.
std::vector<std::uint16_t> buildGridIndices(int quadsPerEdge);

inline constexpr int kMaxQuadsPerEdge = 255; ///< (255 + 1)^2 = 65536 vertices fit in uint16.

} // namespace orbis
