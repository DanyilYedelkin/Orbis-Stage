#include "core/terrain/Quadtree.hpp"

#include "core/geo/Horizon.hpp"
#include "core/terrain/ElevationSource.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace orbis {

namespace {
constexpr double kRootPriority = 1e30; // without roots nothing at all can be drawn
constexpr double kMinDistance = 1.0;   // metres; avoids division by ~0 inside a bounding sphere
} // namespace

bool isAncestor(const NodeKey& a, const NodeKey& b) {
    if (a.face != b.face || a.level >= b.level) {
        return false;
    }
    const int shift = b.level - a.level;
    return (b.x >> shift) == a.x && (b.y >> shift) == a.y;
}

TerrainLod::TerrainLod(const Ellipsoid& ellipsoid, const LodSettings& settings)
    : ellipsoid_(ellipsoid), settings_(settings) {
    if (settings.maxLevel < 0 || settings.maxLevel > NodeKey::kMaxLevel) {
        throw std::invalid_argument("TerrainLod: maxLevel out of range");
    }
}

double TerrainLod::vertexSpacing(const NodeKey& key) const {
    return chunkVertexSpacing(key.region(), settings_.quadsPerEdge, ellipsoid_);
}

BoundingSphere TerrainLod::bounds(const NodeKey& key, const HeightRange& heights) const {
    const FaceRegion r = key.region();
    const double half = r.size / 2.0;

    // Centre: middle of the height range, above the region centre.
    const Vec3d seaCenter =
        ellipsoid_.surfacePointAlong(faceToSphere(r.face, r.uMin + half, r.vMin + half));
    const Vec3d center =
        seaCenter + ellipsoid_.surfaceNormal(seaCenter) * ((heights.min + heights.max) / 2.0);

    // Radius: farthest of 9 surface samples (corners, edge midpoints, centre), each at the lowest
    // and highest height. The surface between samples stays within these extremes, except for a
    // tiny curvature bulge - covered by the 1% margin (verified against real meshes in tests).
    double radius = 0.0;
    for (int j = 0; j <= 2; ++j) {
        for (int i = 0; i <= 2; ++i) {
            const Vec3d sea = ellipsoid_.surfacePointAlong(
                faceToSphere(r.face, r.uMin + i * half, r.vMin + j * half));
            const Vec3d up = ellipsoid_.surfaceNormal(sea);
            radius = std::max({radius, distance(center, sea + up * heights.min),
                               distance(center, sea + up * heights.max)});
        }
    }
    return {center, radius * 1.01 + 1.0};
}

HeightRange TerrainLod::subtreeRange(const NodeKey& key, const HeightRange& meshRange) const {
    // 3 vertex spacings: covers slopes up to ~70 degrees between vertices - real high-resolution
    // mountain data has cliffs steeper than anything the procedural terrain makes.
    const double margin = 3.0 * vertexSpacing(key);
    return {std::max(meshRange.min - margin, 0.0),
            std::min(meshRange.max + margin, kMaxTerrainElevation)};
}

bool TerrainLod::isCulled(const BoundingSphere& b, const LodView& view, LodSelection& out) const {
    ++out.visited;
    if (!view.frustum.intersectsSphere(b.center - view.cameraPosition, b.radius)) {
        ++out.frustumCulled;
        return true;
    }
    if (isBelowHorizon(view.cameraPosition, b.center, b.radius, view.occluder)) {
        ++out.horizonCulled;
        return true;
    }
    return false;
}

double TerrainLod::projectedError(const NodeKey& key, const BoundingSphere& b,
                                  const LodView& view) const {
    // Screen-space error: how many pixels one vertex spacing covers at the node's NEAREST point.
    // Small-angle approximation: size_px = size_m / distance * pixelsPerRadian.
    const double dist = std::max(distance(view.cameraPosition, b.center) - b.radius, kMinDistance);
    return vertexSpacing(key) / dist * view.pixelsPerRadian;
}

LodSelection TerrainLod::select(const LodView& view, const Residency& isResident) const {
    LodSelection out;
    for (const CubeFace face : kAllCubeFaces) {
        const NodeKey root{face, 0, 0, 0};
        const std::optional<HeightRange> mesh = isResident(root);
        const HeightRange range = mesh ? subtreeRange(root, *mesh) : HeightRange{};
        const BoundingSphere b = bounds(root, range);
        if (isCulled(b, view, out)) {
            continue;
        }
        if (!mesh) {
            out.requests.push_back({root, kRootPriority});
            continue;
        }
        visit(root, b, range, view, isResident, out);
    }
    return out;
}

void TerrainLod::visit(const NodeKey& key, const BoundingSphere& b, const HeightRange& range,
                       const LodView& view, const Residency& isResident,
                       LodSelection& out) const {
    out.touched.push_back(key);

    const double error = projectedError(key, b, view);
    if (key.level < settings_.maxLevel && error > settings_.maxPixelError) {
        std::array<NodeKey, 4> children;
        std::array<BoundingSphere, 4> childBounds;
        std::array<HeightRange, 4> childRanges;
        std::array<bool, 4> visible{};
        bool allVisibleResident = true;

        for (std::uint32_t c = 0; c < 4; ++c) {
            children[c] = key.child(c & 1u, c >> 1u);
            // Resident child: tight range from its own mesh. Otherwise inherit ours, which
            // already covers all our descendants.
            const std::optional<HeightRange> mesh = isResident(children[c]);
            childRanges[c] = mesh ? subtreeRange(children[c], *mesh) : range;
            childBounds[c] = bounds(children[c], childRanges[c]);
            visible[c] = !isCulled(childBounds[c], view, out);
            if (visible[c] && !mesh) {
                allVisibleResident = false;
                out.requests.push_back({children[c], error});
            }
        }

        if (allVisibleResident) {
            for (std::size_t c = 0; c < 4; ++c) {
                if (visible[c]) {
                    visit(children[c], childBounds[c], childRanges[c], view, isResident, out);
                }
            }
            return;
        }
        // Fall through: keep showing this (coarser) node until the children arrive.
    }

    out.draw.push_back(key);
    out.deepestLevel = std::max(out.deepestLevel, static_cast<int>(key.level));
}

} // namespace orbis
