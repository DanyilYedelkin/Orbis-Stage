#pragma once

#include "app/Atmosphere.hpp"
#include "app/DetailTexture.hpp"
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

class Clouds;
class LandCoverTexture;
class ShadowCascades;

struct TerrainDrawSettings {
    Vec3d sunDirection{1.0, 0.0, 0.0}; ///< Unit vector towards the sun, ECEF.
    bool wireframe = false;
    bool showLodLevels = false; ///< Debug: tint every chunk by its quadtree level.
    bool geomorph = true;       ///< Smooth LOD transitions (off = classic popping, for comparison).
    float sunIlluminance = 7.0f; ///< Relative units; the tone mapper's exposure is tuned for it.
    bool shadows = true;
    bool landCover = true; ///< Use the land cover map if there is one (off = latitude stylisation).
    bool surfaceDetail = true; ///< Per-pixel detail: bump mapping, albedo variation, frayed edges.
    bool clouds = true;        ///< Volumetric clouds and their shadows.
};

struct TerrainStats {
    std::size_t drawnChunks = 0;
    std::size_t residentChunks = 0;
    std::size_t staleChunks = 0; ///< Drawn, but rebuilding for newer (streamed) data.
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
    /// `clouds` must outlive the terrain (its shadows are drawn on it).
    Terrain(const Ellipsoid& ellipsoid, const ElevationSource& elevation,
            const LodSettings& settings, const Atmosphere& atmosphere, const Clouds& clouds);

    /// `lodCamera` drives selection; normally the real camera, a frozen copy when debugging.
    void update(const Camera& lodCamera, int viewportWidth, int viewportHeight,
                std::size_t maxUploads);
    /// `shadows` may be null (no shadowing).
    void draw(const Camera& camera, float aspect, const TerrainDrawSettings& settings,
              const ShadowCascades* shadows) const;

    /// Depth-only pass with the same geometry and morphing as draw(), for the shadow maps.
    void drawDepth(const gl::ShaderProgram& program, const Camera& camera,
                   const Mat4f& viewProjection, bool geomorph) const;

    /// The elevation data changed inside a spherical cap (unit centre direction, angular radius)
    /// at `version`: resident chunks there whose vertex spacing is finer than `coarsestAffected`
    /// metres get rebuilt. They keep being drawn until the new mesh arrives, so nothing flickers
    /// or opens a hole.
    void invalidate(const Vec3d& centre, double angularRadius, std::uint64_t version,
                    double coarsestAffected);

    /// Land cover map for the colours; null = stylised by latitude. Not owned.
    void setLandCover(const LandCoverTexture* landCover) { landCover_ = landCover; }

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
        double boundingRadius = 0.0;       ///< Around origin, metres.
        std::uint64_t dataVersion = 0;     ///< Elevation data version it was built from.
        std::uint64_t requiredVersion = 0; ///< Newer data exists here: rebuild if dataVersion < this.
        bool stale() const { return dataVersion < requiredVersion; }
    };

    void upload(const NodeKey& key, const ChunkMesh& mesh);
    /// The per-chunk part shared by every pass: offsets, morph window, draw call.
    void drawChunks(const gl::ShaderProgram& program, const Vec3d& viewOrigin,
                    bool withDebugLevel) const;
    void evict();

    Ellipsoid ellipsoid_;
    TerrainLod lod_;
    const LandCoverTexture* landCover_ = nullptr;
    const Clouds* clouds_;
    DetailTexture detail_;
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
