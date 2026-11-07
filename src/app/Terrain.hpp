#pragma once

#include "app/Atmosphere.hpp"
#include "app/DetailTexture.hpp"
#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/geo/Ellipsoid.hpp"
#include "core/scene/Camera.hpp"
#include "core/terrain/ChunkBuilder.hpp"
#include "core/terrain/Climate.hpp"
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
    bool showLodLevels = false;
    bool geomorph = true;
    float sunIlluminance = 7.0f; ///< Relative; the tone mapper's exposure is tuned for it.
    bool shadows = true;
    bool landCover = true;
    bool surfaceDetail = true;
    bool clouds = true;
    float dayOfYear = 196.0f; ///< 0 = 1 January.
};

struct TerrainStats {
    std::size_t drawnChunks = 0;
    std::size_t residentChunks = 0;
    std::size_t staleChunks = 0; ///< Drawn, awaiting a rebuild with newer tile data.
    std::size_t pendingBuilds = 0;
    std::size_t runningBuilds = 0;
    std::size_t drawnTriangles = 0;
    int deepestLevel = 0;
    int visited = 0;
    int frustumCulled = 0;
    int horizonCulled = 0;
};

/// Quadtree selection -> background mesh builds -> GPU chunk cache. update() uploads a bounded
/// number of finished meshes, selects, schedules what is missing and evicts unused chunks.
class Terrain {
public:
    /// `clouds` must outlive the terrain.
    Terrain(const Ellipsoid& ellipsoid, const ElevationSource& elevation,
            const LodSettings& settings, const Atmosphere& atmosphere, const Clouds& clouds,
            const Climate& climate);

    static constexpr unsigned kOceanicityUnit = 13;

    /// `lodCamera` drives selection; normally the real camera, a frozen copy when debugging.
    void update(const Camera& lodCamera, int viewportWidth, int viewportHeight,
                std::size_t maxUploads);
    /// `shadows` may be null (no shadowing).
    void draw(const Camera& camera, float aspect, const TerrainDrawSettings& settings,
              const ShadowCascades* shadows) const;

    /// Depth-only pass with the same geometry and morphing as draw(), for the shadow maps.
    void drawDepth(const gl::ShaderProgram& program, const Camera& camera,
                   const Mat4f& viewProjection, bool geomorph) const;

    /// Elevation data changed at `version` inside a spherical cap: chunks there finer than
    /// `coarsestAffected` metres are rebuilt, and drawn as they are until then.
    void invalidate(const Vec3d& centre, double angularRadius, std::uint64_t version,
                    double coarsestAffected);

    /// Null = stylised by latitude. Not owned.
    void setLandCover(const LandCoverTexture* landCover) { landCover_ = landCover; }

    /// Last selection requested nothing and no build is in flight: the image is final.
    bool settled() const;

    LodSettings& lodSettings() { return lod_.settings(); }
    const TerrainStats& stats() const { return stats_; }

private:
    struct GpuChunk {
        gl::VertexArray vao;
        gl::Buffer vertices;
        Vec3d origin;
        HeightRange heights; ///< Of the mesh itself, for tight quadtree bounds.
        std::uint64_t lastUsedFrame = 0;
        std::uint8_t level = 0;
        double boundingRadius = 0.0;
        std::uint64_t dataVersion = 0;
        std::uint64_t requiredVersion = 0;
        bool stale() const { return dataVersion < requiredVersion; }
    };

    void upload(const NodeKey& key, const ChunkMesh& mesh);
    void drawChunks(const gl::ShaderProgram& program, const Vec3d& viewOrigin,
                    bool withDebugLevel) const;
    void evict();

    Ellipsoid ellipsoid_;
    TerrainLod lod_;
    const LandCoverTexture* landCover_ = nullptr;
    const Clouds* clouds_;
    DetailTexture detail_;
    gl::Texture oceanicity_;
    gl::ShaderProgram shader_;
    gl::Buffer indexBuffer_; ///< Shared by every chunk.
    GLsizei indexCount_ = 0;
    std::size_t trianglesPerChunk_ = 0;

    std::unordered_map<NodeKey, GpuChunk, NodeKeyHash> chunks_;
    std::vector<NodeKey> drawList_;
    std::uint64_t frame_ = 0;
    bool lastSelectionComplete_ = false;
    Vec3d lodCameraPosition_; ///< Differs from the camera when the LOD is frozen.
    double pixelsPerRadian_ = 1.0;
    TerrainStats stats_;

    // Last: the workers join before anything they use is destroyed.
    ChunkBuilder builder_;
};

} // namespace orbis
