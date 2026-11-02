#include "core/terrain/ChunkMesh.hpp"
#include "core/terrain/HeightField.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <vector>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
const Ellipsoid kWgs84 = Ellipsoid::wgs84();
const HeightField kField(20261102);

Vec3d worldPos(const ChunkMesh& m, std::size_t i) {
    return m.origin + Vec3d(m.vertices[i].position);
}
} // namespace

TEST_CASE("index buffer layout: grid then skirt", "[chunk]") {
    const int n = 16;
    const auto indices = buildChunkIndices(n);
    REQUIRE(indices.size() ==
            static_cast<std::size_t>(chunkGridIndexCount(n) + chunkSkirtIndexCount(n)));
    const auto maxIndex = *std::max_element(indices.begin(), indices.end());
    CHECK(maxIndex == chunkVertexCount(n) - 1);
}

TEST_CASE("invalid resolution is rejected", "[chunk]") {
    CHECK_THROWS(buildChunkIndices(0));
    CHECK_THROWS(buildChunkIndices(kMaxQuadsPerEdge + 1));
}

TEST_CASE("border walk visits every border vertex exactly once", "[chunk]") {
    const int n = 8;
    std::set<int> seen;
    for (int k = 0; k < chunkSkirtVertexCount(n); ++k) {
        const int g = boundaryGridIndex(k, n);
        const int i = g % (n + 1);
        const int j = g / (n + 1);
        REQUIRE((i == 0 || i == n || j == 0 || j == n));
        seen.insert(g);
    }
    CHECK(seen.size() == static_cast<std::size_t>(4 * n));
}

TEST_CASE("chunk mesh basics", "[chunk]") {
    const int n = 16;
    const ChunkMesh mesh = buildChunkMesh({CubeFace::PosZ, -0.25, 0.0, 0.25}, n, kWgs84, kField);
    REQUIRE(mesh.vertices.size() == static_cast<std::size_t>(chunkVertexCount(n)));

    for (std::size_t i = 0; i < static_cast<std::size_t>(chunkGridVertexCount(n)); ++i) {
        const TerrainVertex& v = mesh.vertices[i];
        REQUIRE_THAT(static_cast<double>(length(v.normal)), WithinAbs(1.0, 1e-5));
        REQUIRE(static_cast<double>(length(v.position)) <= mesh.boundingRadius * 1.000001);
    }
}

TEST_CASE("surface triangles face outwards", "[chunk]") {
    const int n = 16;
    const auto indices = buildChunkIndices(n);
    const auto gridIndices = static_cast<std::size_t>(chunkGridIndexCount(n));
    for (const CubeFace face : kAllCubeFaces) {
        const ChunkMesh mesh = buildChunkMesh({face, -1.0, -1.0, 2.0}, n, kWgs84, kField);
        for (std::size_t t = 0; t < gridIndices; t += 3) {
            const Vec3d a = worldPos(mesh, indices[t]);
            const Vec3d triNormal =
                cross(worldPos(mesh, indices[t + 1]) - a, worldPos(mesh, indices[t + 2]) - a);
            REQUIRE(dot(triNormal, a) > 0.0); // a is also the direction away from the centre
        }
    }
}

TEST_CASE("skirts hang below the border and face away from the chunk", "[chunk][skirt]") {
    const int n = 16;
    const FaceRegion region{CubeFace::NegY, 0.25, -0.5, 0.125};
    const ChunkMesh mesh = buildChunkMesh(region, n, kWgs84, kField);
    const auto indices = buildChunkIndices(n);
    const double depth = chunkSkirtDepth(region, n, kWgs84);
    const Vec3d chunkCenter = mesh.origin;

    for (int k = 0; k < chunkSkirtVertexCount(n); ++k) {
        const auto top = static_cast<std::size_t>(boundaryGridIndex(k, n));
        const auto bottom = static_cast<std::size_t>(chunkGridVertexCount(n) + k);
        REQUIRE_THAT(distance(worldPos(mesh, top), worldPos(mesh, bottom)), WithinAbs(depth, 0.1));
        REQUIRE(length(worldPos(mesh, bottom)) < length(worldPos(mesh, top)));
    }

    for (auto t = static_cast<std::size_t>(chunkGridIndexCount(n)); t < indices.size(); t += 3) {
        const Vec3d a = worldPos(mesh, indices[t]);
        const Vec3d b = worldPos(mesh, indices[t + 1]);
        const Vec3d c = worldPos(mesh, indices[t + 2]);
        const Vec3d triNormal = cross(b - a, c - a);
        const Vec3d mid = (a + b + c) / 3.0;
        // "Outwards" = away from the chunk centre, measured along the surface (remove the
        // vertical part so the planet's curvature does not dominate).
        const Vec3d up = normalize(mid);
        Vec3d away = mid - chunkCenter;
        away -= up * dot(away, up);
        REQUIRE(dot(triNormal, away) > 0.0);
    }
}

TEST_CASE("adjacent chunks share identical border vertices and normals", "[chunk]") {
    // Two neighbours along u on the same face: right edge of A == left edge of B.
    const int n = 32;
    const double size = 0.25;
    const ChunkMesh a = buildChunkMesh({CubeFace::PosX, 0.0, 0.0, size}, n, kWgs84, kField);
    const ChunkMesh b = buildChunkMesh({CubeFace::PosX, size, 0.0, size}, n, kWgs84, kField);

    // Positions are stored as float relative to each chunk's origin, so each side rounds with
    // a step of ~radius * FLT_EPSILON. That bounds the mismatch; it shrinks with the chunk size,
    // which is exactly why LOD + camera-relative rendering gives sub-mm precision up close.
    const double tolerance = 2.0 * std::max(a.boundingRadius, b.boundingRadius) *
                             static_cast<double>(std::numeric_limits<float>::epsilon());

    for (int j = 0; j <= n; ++j) {
        const auto ia = static_cast<std::size_t>(j * (n + 1) + n);
        const auto ib = static_cast<std::size_t>(j * (n + 1));
        REQUIRE(distance(worldPos(a, ia), worldPos(b, ib)) <= tolerance);
        const TerrainVertex& va = a.vertices[ia];
        const TerrainVertex& vb = b.vertices[ib];
        REQUIRE(va.normal.x == vb.normal.x); // computed from the direction alone => bitwise
        REQUIRE(va.normal.y == vb.normal.y);
        REQUIRE(va.normal.z == vb.normal.z);
    }
}

TEST_CASE("relative positions keep float precision high", "[chunk][precision]") {
    // A small, deep-LOD-sized chunk (~3 km). Reconstruct its first vertex independently in
    // double precision and compare against what the mesh stores.
    const FaceRegion region{CubeFace::PosY, 0.1, 0.1, 0.0005};
    const int n = 8;
    const ChunkMesh mesh = buildChunkMesh(region, n, kWgs84, kField);

    const double spacing = chunkVertexSpacing(region, n, kWgs84);
    const Vec3d dir = faceToSphere(region.face, region.uMin, region.vMin);
    const Vec3d seaLevel = kWgs84.surfacePointAlong(dir);
    const Vec3d exact =
        seaLevel + kWgs84.surfaceNormal(seaLevel) * std::max(kField.elevation(dir, spacing), 0.0);

    // Camera-relative float storage: error far below a millimetre.
    CHECK(distance(worldPos(mesh, 0), exact) < 1e-3);

    // The same vertex stored as absolute float ECEF: off by centimetres to decimetres.
    const Vec3d absoluteFloat = Vec3d(Vec3f(exact));
    CHECK(distance(absoluteFloat, exact) > 0.01);
}
