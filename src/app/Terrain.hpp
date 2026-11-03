#pragma once

#include "app/Atmosphere.hpp"
#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/geo/Ellipsoid.hpp"
#include "core/scene/Camera.hpp"
#include "core/terrain/ChunkBuilder.hpp"
#include "core/terrain/ElevationSource.hpp"
#include "core/terrain/Quadtree.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace orbis {

struct TerrainDrawSettings {
    Vec3d sunDirection{1.0, 0.0, 0.0}; ///< Unit vector towards the sun, ECEF.
    bool wireframe = false;
    bool showLodLevels = false; ///< Debug: tint every chunk by its quadtree level.
    bool geomorph = true;       ///< Smooth LOD transitions (off = classic popping, for comparison).
    float sunIlluminance = 7.0f; ///< Relative units; the tone mapper's exposure is tuned for it.
};

struct TerrainStats {
    std::size_t drawnChunks = 0;
    std::size_t residentChunks = 0;
    std::size_t pendingBuilds = 0;
    std::size_t runningBuilds = 0;
    std::size_t drawnTriangles = 0;
    int deepestLevel = 0;
    int visited = 0;
    int frustumCulled = 0;
    int horizonCulled = 0;
};

/// Streaming LOD terrain: quadtree selection -> background mesh builds -> GPU chunk cache.
///
/// Per frame (update):
///   1. upload finished meshes from the worker threads (bounded per frame: no hitches)
///   2. select nodes for the current view (pure function, see TerrainLod)
///   3. hand the missing nodes to the builder, most urgent first
///   4. evict chunks nobody has used for a while (bounded GPU memory)
class Terrain {
public:
    Terrain(const Ellipsoid& ellipsoid, const ElevationSource& elevation,
            const LodSettings& settings, const Atmosphere& atmosphere);

    /// `lodCamera` drives selection; normally the real camera, a frozen copy when debugging.
    void update(const Camera& lodCamera, int viewportWidth, int viewportHeight,
                std::size_t maxUploads);
    void draw(const Camera& camera, float aspect, const TerrainDrawSettings& settings) const;

    /// Last selection requested nothing and no build is in flight: the image is final.
    bool settled() const;

    LodSettings& lodSettings() { return lod_.settings(); }
    const TerrainStats& stats() const { return stats_; }

private:
    struct GpuChunk {
        gl::VertexArray vao;
        gl::Buffer vertices;
        Vec3d origin;            ///< DOUBLE on the CPU; only (origin - camera) goes to the GPU.
        HeightRange heights;     ///< Of the mesh itself: gives the quadtree tight bounds.
        std::uint64_t lastUsedFrame = 0;
        std::uint8_t level = 0;
    };

    void upload(const NodeKey& key, const ChunkMesh& mesh);
    void evict();

    Ellipsoid ellipsoid_;
    TerrainLod lod_;
    gl::ShaderProgram shader_;
    gl::Buffer indexBuffer_; ///< One topology (grid + skirt), shared by every chunk.
    GLsizei indexCount_ = 0;
    std::size_t trianglesPerChunk_ = 0;

    std::unordered_map<NodeKey, GpuChunk, NodeKeyHash> chunks_;
    std::vector<NodeKey> drawList_;
    std::uint64_t frame_ = 0;
    bool lastSelectionComplete_ = false;
    Vec3d lodCameraPosition_;     ///< Camera the LOD was selected for (differs when frozen).
    double pixelsPerRadian_ = 1.0;
    TerrainStats stats_;

    // Last member: destroyed first, so worker threads are joined before anything they could
    // still be using goes away.
    ChunkBuilder builder_;
};

} // namespace orbis
