#pragma once

#include "app/gl/GlHandle.hpp"
#include "app/gl/ShaderProgram.hpp"
#include "core/math/Vec3.hpp"
#include "core/scene/Camera.hpp"
#include "core/terrain/ChunkMesh.hpp"

#include <cstddef>
#include <vector>

namespace orbis {

struct TerrainDrawSettings {
    Vec3d sunDirection{1.0, 0.0, 0.0}; ///< Unit vector towards the sun, ECEF.
    bool wireframe = false;
};

/// Owns the GPU copies of the terrain chunks and draws them camera-relative.
class TerrainRenderer {
public:
    explicit TerrainRenderer(int quadsPerEdge);

    /// Uploads CPU meshes (must all use the quadsPerEdge given to the constructor).
    void upload(const std::vector<ChunkMesh>& meshes);

    void draw(const Camera& camera, float aspect, const TerrainDrawSettings& settings) const;

    std::size_t chunkCount() const { return chunks_.size(); }
    std::size_t triangleCount() const { return chunks_.size() * static_cast<std::size_t>(indexCount_) / 3; }

private:
    struct GpuChunk {
        gl::VertexArray vao;
        gl::Buffer vertices;
        Vec3d origin; ///< Kept in DOUBLE on the CPU; only (origin - camera) goes to the GPU.
    };

    int quadsPerEdge_;
    gl::ShaderProgram shader_;
    gl::Buffer indexBuffer_; ///< One grid topology, shared by every chunk.
    GLsizei indexCount_ = 0;
    std::vector<GpuChunk> chunks_;
};

} // namespace orbis
