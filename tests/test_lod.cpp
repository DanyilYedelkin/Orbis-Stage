#include "core/terrain/HeightField.hpp"
#include "core/scene/Camera.hpp"
#include "core/terrain/Quadtree.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <algorithm>
#include <optional>
#include <random>
#include <unordered_set>

using namespace orbis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
const Ellipsoid kWgs84 = Ellipsoid::wgs84();

/// A view like the renderer builds: 1600x900, 60 degree vertical FOV.
LodView makeView(const Geodetic& where, double headingDeg, double pitchDeg) {
    Camera cam(kWgs84);
    cam.setPosition(where);
    cam.setOrientation(headingDeg * kDeg, pitchDeg * kDeg);
    const CameraBasis b = cam.basis();
    const double fov = cam.verticalFov;
    return {
        .cameraPosition = cam.position(),
        .frustum = Frustum::fromCamera(b.forward, b.right, b.up, fov, 16.0 / 9.0, cam.nearPlane),
        .pixelsPerRadian = 900.0 / (2.0 * std::tan(fov / 2.0)),
        .occluder = kWgs84,
    };
}

const HeightField kField(20261102);

/// "Everything is loaded": the height range a real mesh would report, approximated by sampling
/// the height field on a 5x5 grid at the node's resolution (exact meshes would be too slow here).
std::optional<HeightRange> everythingResident(const NodeKey& key) {
    const FaceRegion r = key.region();
    const double spacing = chunkVertexSpacing(r, 32, kWgs84);
    HeightRange range{1e9, -1e9};
    for (int j = 0; j <= 4; ++j) {
        for (int i = 0; i <= 4; ++i) {
            const double h = std::max(
                kField.elevation(faceToSphere(r.face, r.uMin + r.size * i / 4.0,
                                              r.vMin + r.size * j / 4.0),
                                 spacing),
                0.0);
            range.min = std::min(range.min, h);
            range.max = std::max(range.max, h);
        }
    }
    return range;
}
const TerrainLod::Residency kEverythingResident = everythingResident;
const TerrainLod::Residency kNothingResident = [](const NodeKey&) {
    return std::optional<HeightRange>{};
};

/// No drawn node may be an ancestor of another: that would mean overlapping geometry.
bool hasOverlap(const std::vector<NodeKey>& nodes) {
    for (const NodeKey& a : nodes) {
        for (const NodeKey& b : nodes) {
            if (isAncestor(a, b)) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

TEST_CASE("node keys: children tile the parent exactly", "[lod]") {
    const NodeKey parent{CubeFace::NegZ, 5, 17, 9};
    const FaceRegion p = parent.region();
    for (std::uint32_t c = 0; c < 4; ++c) {
        const NodeKey child = parent.child(c & 1u, c >> 1u);
        const FaceRegion r = child.region();
        CHECK(r.size == p.size / 2.0);
        CHECK(r.uMin == p.uMin + (c & 1u) * r.size);
        CHECK(r.vMin == p.vMin + (c >> 1u) * r.size);
        CHECK(isAncestor(parent, child));
        CHECK_FALSE(isAncestor(child, parent));
    }
}

TEST_CASE("node keys pack uniquely", "[lod]") {
    std::unordered_set<std::uint64_t> seen;
    for (const CubeFace face : kAllCubeFaces) {
        for (std::uint8_t level = 0; level <= 3; ++level) {
            const std::uint32_t cells = 1u << level;
            for (std::uint32_t y = 0; y < cells; ++y) {
                for (std::uint32_t x = 0; x < cells; ++x) {
                    REQUIRE(seen.insert(NodeKey{face, level, x, y}.packed()).second);
                }
            }
        }
    }
}

TEST_CASE("analytic bounds contain the real mesh", "[lod][bounds]") {
    // Bounds are computed BEFORE the mesh exists (for culling). They must still contain every
    // surface vertex, or visible terrain could be culled.
    const HeightField& field = kField;
    const TerrainLod lod(kWgs84, {});
    std::mt19937 rng(3);
    for (int level : {0, 2, 5, 9, 14, 18}) {
        for (int trial = 0; trial < 4; ++trial) {
            const std::uint32_t cells = 1u << level;
            std::uniform_int_distribution<std::uint32_t> cell(0, cells - 1);
            const NodeKey key{kAllCubeFaces[static_cast<std::size_t>(trial) % 6],
                              static_cast<std::uint8_t>(level), cell(rng), cell(rng)};
            const BoundingSphere b = lod.bounds(key);
            const ChunkMesh mesh = buildChunkMesh(key.region(), 16, kWgs84, field);
            for (int i = 0; i < chunkGridVertexCount(16); ++i) {
                const Vec3d p = mesh.origin + Vec3d(mesh.vertices[static_cast<std::size_t>(i)].position);
                REQUIRE(distance(p, b.center) <= b.radius);
            }
        }
    }
}

TEST_CASE("subtree height range contains every descendant", "[lod][bounds]") {
    // The quadtree bounds a NOT-yet-built child by its parent's subtree range. If a child could
    // poke out of it, culling could remove visible terrain - so check against real meshes.
    const TerrainLod lod(kWgs84, {});
    std::mt19937 rng(7);
    for (int level : {2, 5, 8, 11, 14, 16}) {
        for (int trial = 0; trial < 3; ++trial) {
            const std::uint32_t cells = 1u << level;
            std::uniform_int_distribution<std::uint32_t> cell(0, cells - 1);
            const NodeKey parent{kAllCubeFaces[static_cast<std::size_t>(level + trial) % 6],
                                 static_cast<std::uint8_t>(level), cell(rng), cell(rng)};
            const ChunkMesh pm = buildChunkMesh(parent.region(), 32, kWgs84, kField);
            const HeightRange range = lod.subtreeRange(parent, {pm.minHeight, pm.maxHeight});

            for (std::uint32_t c = 0; c < 4; ++c) {
                const NodeKey child = parent.child(c & 1u, c >> 1u);
                for (const NodeKey& k : {child, child.child(1, 0)}) {
                    const ChunkMesh m = buildChunkMesh(k.region(), 32, kWgs84, kField);
                    REQUIRE(m.minHeight >= range.min);
                    REQUIRE(m.maxHeight <= range.max);
                }
            }
        }
    }
}

TEST_CASE("height field never exceeds its declared maximum", "[lod][bounds]") {
    const HeightField field(20261102);
    std::mt19937_64 rng(11);
    std::normal_distribution<double> gauss;
    for (int i = 0; i < 50000; ++i) {
        const Vec3d dir = normalize(Vec3d{gauss(rng), gauss(rng), gauss(rng)});
        REQUIRE(field.elevation(dir) < HeightField::maxElevation());
    }
}

TEST_CASE("from deep space only the roots are drawn", "[lod][select]") {
    // Note: a bounding sphere around a whole cube face (90 degrees of planet) is too large to
    // ever fall completely behind the horizon, so even the far-side root is kept. That is fine:
    // roots are cheap, and the horizon test does its real work on smaller nodes (next test).
    const TerrainLod lod(kWgs84, {});
    const LodSelection s = lod.select(makeView({0.0, 0.0, 1.0e8}, 0.0, -89.0), kEverythingResident);
    CHECK(s.deepestLevel == 0);
    CHECK(s.draw.size() <= 6);
    CHECK(s.requests.empty());
}

TEST_CASE("near the ground the selection refines to the deepest level", "[lod][select]") {
    const TerrainLod lod(kWgs84, {});
    const LodSelection s =
        lod.select(makeView({46.0 * kDeg, 7.0 * kDeg, 300.0}, 30.0, -10.0), kEverythingResident);
    CHECK(s.deepestLevel == lod.settings().maxLevel);
    CHECK_FALSE(hasOverlap(s.draw));
    CHECK(s.frustumCulled > 0);
    CHECK(s.horizonCulled > 0);
    // Without culling and with loose bounds this was ~24 000 chunks (a regression this test
    // now guards against); with tight height ranges and the exact horizon: about a thousand.
    CHECK(s.draw.size() < 2500);
}

TEST_CASE("missing data: coarse fallback is drawn and children are requested", "[lod][select]") {
    const TerrainLod lod(kWgs84, {});
    const LodView view = makeView({10.0 * kDeg, 20.0 * kDeg, 2000.0}, 0.0, -20.0);

    // Nothing resident: roots requested, nothing drawn.
    const LodSelection empty = lod.select(view, kNothingResident);
    CHECK(empty.draw.empty());
    CHECK_FALSE(empty.requests.empty());
    for (const NodeRequest& r : empty.requests) {
        CHECK(r.key.level == 0);
    }

    // Only roots resident: the roots are drawn (no holes) and level-1 children requested.
    const LodSelection roots = lod.select(view, [](const NodeKey& k) {
        return k.level == 0 ? everythingResident(k) : std::nullopt;
    });
    CHECK_FALSE(roots.draw.empty());
    CHECK(roots.deepestLevel == 0);
    for (const NodeRequest& r : roots.requests) {
        CHECK(r.key.level == 1);
    }
}

TEST_CASE("lower pixel error means more detail", "[lod][select]") {
    TerrainLod lod(kWgs84, {});
    const LodView view = makeView({30.0 * kDeg, 30.0 * kDeg, 50000.0}, 0.0, -30.0);
    lod.settings().maxPixelError = 16.0;
    const auto coarse = lod.select(view, kEverythingResident).draw.size();
    lod.settings().maxPixelError = 2.0;
    const auto fine = lod.select(view, kEverythingResident).draw.size();
    CHECK(fine > coarse * 4);
}
