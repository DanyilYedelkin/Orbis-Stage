#include "core/terrain/ChunkMesh.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <limits>
#include <vector>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
const Ellipsoid kWgs84 = Ellipsoid::wgs84();
const HeightField kField(20261102);
} // namespace

TEST_CASE("grid indices", "[chunk]") {
    const int n = 16;
    const auto indices = buildGridIndices(n);
    REQUIRE(indices.size() == static_cast<std::size_t>(n * n * 6));
    const auto maxIndex = *std::max_element(indices.begin(), indices.end());
    CHECK(maxIndex == (n + 1) * (n + 1) - 1);
}

TEST_CASE("invalid resolution is rejected", "[chunk]") {
    CHECK_THROWS(buildGridIndices(0));
    CHECK_THROWS(buildGridIndices(kMaxQuadsPerEdge + 1));
}

TEST_CASE("chunk mesh basics", "[chunk]") {
    const int n = 16;
    const ChunkMesh mesh = buildChunkMesh({CubeFace::PosZ, -0.25, 0.0, 0.25}, n, kWgs84, kField);
    REQUIRE(mesh.vertices.size() == static_cast<std::size_t>((n + 1) * (n + 1)));

    for (const TerrainVertex& v : mesh.vertices) {
        REQUIRE_THAT(static_cast<double>(length(v.normal)), WithinAbs(1.0, 1e-5));
        REQUIRE(static_cast<double>(length(v.position)) <= mesh.boundingRadius * 1.000001);
    }
}

TEST_CASE("triangles face outwards (counter-clockwise from outside)", "[chunk]") {
    const int n = 16;
    const auto indices = buildGridIndices(n);
    for (const CubeFace face : kAllCubeFaces) {
        const ChunkMesh mesh = buildChunkMesh({face, -1.0, -1.0, 2.0}, n, kWgs84, kField);
        for (std::size_t t = 0; t < indices.size(); t += 3) {
            const auto world = [&](std::size_t k) {
                return mesh.origin + Vec3d(mesh.vertices[indices[t + k]].position);
            };
            const Vec3d a = world(0);
            const Vec3d triNormal = cross(world(1) - a, world(2) - a);
            REQUIRE(dot(triNormal, a) > 0.0); // a is also the direction away from the centre
        }
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
    const double tolerance =
        2.0 * std::max(a.boundingRadius, b.boundingRadius) * static_cast<double>(std::numeric_limits<float>::epsilon());

    for (int j = 0; j <= n; ++j) {
        const TerrainVertex& va = a.vertices[static_cast<std::size_t>(j * (n + 1) + n)];
        const TerrainVertex& vb = b.vertices[static_cast<std::size_t>(j * (n + 1))];
        const Vec3d pa = a.origin + Vec3d(va.position);
        const Vec3d pb = b.origin + Vec3d(vb.position);
        REQUIRE(distance(pa, pb) <= tolerance);
        REQUIRE(va.normal.x == vb.normal.x); // computed from the direction alone => bitwise
        REQUIRE(va.normal.y == vb.normal.y);
        REQUIRE(va.normal.z == vb.normal.z);
    }
}

TEST_CASE("relative positions keep float precision high", "[chunk][precision]") {
    // A small, deep-LOD-sized chunk (~3 km). Reconstruct its first vertex independently in
    // double precision and compare against what the mesh stores.
    const FaceRegion region{CubeFace::PosY, 0.1, 0.1, 0.0005};
    const ChunkMesh mesh = buildChunkMesh(region, 8, kWgs84, kField);

    const Vec3d dir = faceToSphere(region.face, region.uMin, region.vMin);
    const Vec3d seaLevel = kWgs84.surfacePointAlong(dir);
    const Vec3d exact =
        seaLevel + kWgs84.surfaceNormal(seaLevel) * std::max(kField.elevation(dir), 0.0);

    // Camera-relative float storage: error far below a millimetre.
    const Vec3d stored = mesh.origin + Vec3d(mesh.vertices.front().position);
    CHECK(distance(stored, exact) < 1e-3);

    // The same vertex stored as absolute float ECEF: off by centimetres to decimetres.
    const Vec3d absoluteFloat = Vec3d(Vec3f(exact));
    CHECK(distance(absoluteFloat, exact) > 0.01);
}
