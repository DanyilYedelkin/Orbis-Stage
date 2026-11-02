#pragma once

#include "core/geo/Ellipsoid.hpp"
#include "core/math/Frustum.hpp"
#include "core/terrain/ChunkMesh.hpp"
#include "core/terrain/CubeSphere.hpp"
#include "core/terrain/ElevationSource.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace orbis {

/// Address of a quadtree node: cube face, depth, and integer cell coordinates at that depth
/// (x, y in [0, 2^level)). Integer keys instead of pointers: trivially hashable, copyable to
/// worker threads, and the face region follows exactly from them.
struct NodeKey {
    CubeFace face = CubeFace::PosX;
    std::uint8_t level = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    static constexpr int kMaxLevel = 27; ///< Limited by the 28-bit x/y fields in packed().

    /// 3 bits face | 5 bits level | 28 bits x | 28 bits y.
    std::uint64_t packed() const {
        return (static_cast<std::uint64_t>(face) << 61) |
               (static_cast<std::uint64_t>(level) << 56) | (static_cast<std::uint64_t>(x) << 28) |
               static_cast<std::uint64_t>(y);
    }

    /// dx, dy in {0, 1}.
    NodeKey child(std::uint32_t dx, std::uint32_t dy) const {
        return {face, static_cast<std::uint8_t>(level + 1), x * 2 + dx, y * 2 + dy};
    }

    /// Region on the face. Sizes are powers of two, so every value is exact in double and
    /// neighbours agree bit-for-bit on their shared borders.
    FaceRegion region() const {
        const double size = 2.0 / static_cast<double>(std::uint64_t{1} << level);
        return {face, -1.0 + x * size, -1.0 + y * size, size};
    }

    bool operator==(const NodeKey&) const = default;
};

struct NodeKeyHash {
    std::size_t operator()(const NodeKey& k) const noexcept {
        // SplitMix64 finaliser: packed keys of neighbours differ in few bits; mix them well.
        std::uint64_t z = k.packed() + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<std::size_t>(z ^ (z >> 31));
    }
};

/// True if `a` is a strict ancestor of `b`.
bool isAncestor(const NodeKey& a, const NodeKey& b);

/// Height interval above the ellipsoid, metres.
struct HeightRange {
    double min = 0.0;
    double max = kMaxTerrainElevation;
};

struct BoundingSphere {
    Vec3d center; ///< ECEF.
    double radius = 0.0;
};

struct LodSettings {
    int quadsPerEdge = 32;
    int maxLevel = 18;          ///< ~1.2 m between vertices at the deepest level.
    double maxPixelError = 4.0; ///< Refine until vertex spacing projects to <= this many pixels.
};

/// Everything selection needs to know about the viewer.
struct LodView {
    Vec3d cameraPosition;    ///< ECEF.
    Frustum frustum;         ///< Camera-relative.
    double pixelsPerRadian;  ///< viewportHeight / (2 tan(verticalFov / 2)).
    Ellipsoid occluder = Ellipsoid::wgs84(); ///< Terrain never goes below it (horizon culling).
};

struct NodeRequest {
    NodeKey key;
    double priority = 0.0; ///< Larger = more urgent (projected error that wants this node).
};

struct LodSelection {
    std::vector<NodeKey> draw;            ///< Nodes to render this frame (all resident).
    std::vector<NodeKey> touched;         ///< Resident nodes used this frame (keep in cache).
    std::vector<NodeRequest> requests;    ///< Missing nodes that would improve the image.
    int visited = 0;
    int frustumCulled = 0;
    int horizonCulled = 0;
    int deepestLevel = 0;
};

/// Chunked LOD over the 6 cube faces.
///
/// select() is a PURE function of (view, which nodes are resident): no hidden state, so it is
/// trivially testable and could run on another thread. Rules per node:
///   1. culled by frustum or horizon            -> skip the whole subtree
///   2. projected error small enough            -> draw it
///   3. otherwise refine: if every VISIBLE child is resident, recurse into them; if not, draw
///      this node for now and request the missing children. The image never has holes, it just
///      gets sharper as data arrives.
class TerrainLod {
public:
    TerrainLod(const Ellipsoid& ellipsoid, const LodSettings& settings);

    /// Returns the height range of the node's mesh if it is resident, nullopt otherwise.
    using Residency = std::function<std::optional<HeightRange>(const NodeKey&)>;
    LodSelection select(const LodView& view, const Residency& isResident) const;

    /// Conservative sphere containing the node's surface for any elevation within `heights`.
    /// The default (0 .. max terrain height) is valid before ANY data exists.
    BoundingSphere bounds(const NodeKey& key, const HeightRange& heights = {}) const;

    /// Height range guaranteed to contain the node AND all of its descendants, given the range
    /// of the node's own mesh. Descendants add finer detail and resolve peaks that fell between
    /// this node's vertices; both are bounded by a margin proportional to the vertex spacing
    /// (verified against real meshes in the tests).
    HeightRange subtreeRange(const NodeKey& key, const HeightRange& meshRange) const;
    double vertexSpacing(const NodeKey& key) const;

    LodSettings& settings() { return settings_; }
    const LodSettings& settings() const { return settings_; }

private:
    bool isCulled(const BoundingSphere& b, const LodView& view, LodSelection& out) const;
    double projectedError(const NodeKey& key, const BoundingSphere& b, const LodView& view) const;
    void visit(const NodeKey& key, const BoundingSphere& b, const HeightRange& range,
               const LodView& view, const Residency& isResident, LodSelection& out) const;

    Ellipsoid ellipsoid_;
    LodSettings settings_;
};

} // namespace orbis
