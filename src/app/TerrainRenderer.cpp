#include "app/TerrainRenderer.hpp"

#include "EmbeddedShaders.hpp"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace orbis {

TerrainRenderer::TerrainRenderer(int quadsPerEdge)
    : quadsPerEdge_(quadsPerEdge),
      shader_(shaders::terrain_vert, shaders::terrain_frag, "terrain"),
      indexBuffer_(gl::createBuffer()) {
    const std::vector<std::uint16_t> indices = buildGridIndices(quadsPerEdge);
    indexCount_ = static_cast<GLsizei>(indices.size());
    // Immutable storage (flags = 0): the driver knows the data never changes and can place it
    // in the fastest memory.
    glNamedBufferStorage(indexBuffer_.get(),
                         static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint16_t)),
                         indices.data(), 0);
}

void TerrainRenderer::upload(const std::vector<ChunkMesh>& meshes) {
    const std::size_t expectedVertices = static_cast<std::size_t>(quadsPerEdge_ + 1) *
                                         static_cast<std::size_t>(quadsPerEdge_ + 1);
    chunks_.reserve(chunks_.size() + meshes.size());

    for (const ChunkMesh& mesh : meshes) {
        if (mesh.vertices.size() != expectedVertices) {
            throw std::invalid_argument("TerrainRenderer::upload: chunk resolution mismatch");
        }

        GpuChunk chunk{gl::createVertexArray(), gl::createBuffer(), mesh.origin};
        glNamedBufferStorage(chunk.vertices.get(),
                             static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(TerrainVertex)),
                             mesh.vertices.data(), 0);

        // Vertex layout, described once per VAO with DSA (no glBind* juggling).
        const GLuint vao = chunk.vao.get();
        glVertexArrayVertexBuffer(vao, 0, chunk.vertices.get(), 0, sizeof(TerrainVertex));
        glVertexArrayElementBuffer(vao, indexBuffer_.get());

        glEnableVertexArrayAttrib(vao, 0);
        glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, position));
        glVertexArrayAttribBinding(vao, 0, 0);

        glEnableVertexArrayAttrib(vao, 1);
        glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, normal));
        glVertexArrayAttribBinding(vao, 1, 0);

        glEnableVertexArrayAttrib(vao, 2);
        glVertexArrayAttribFormat(vao, 2, 1, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, elevation));
        glVertexArrayAttribBinding(vao, 2, 0);

        chunks_.push_back(std::move(chunk));
    }
}

void TerrainRenderer::draw(const Camera& camera, float aspect,
                           const TerrainDrawSettings& settings) const {
    const Vec3d& cameraPos = camera.position();
    const Mat4f viewProjection = camera.projectionMatrix(aspect) * camera.viewRotationMatrix();

    shader_.use();
    shader_.set("uViewProjection", viewProjection);
    shader_.set("uSunDirection", Vec3f(normalize(settings.sunDirection)));
    shader_.set("uPlanetCenter", Vec3f(-cameraPos)); // only used as a direction, float is fine
    shader_.set("uWireframe", settings.wireframe ? 1 : 0);

    glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

    for (const GpuChunk& chunk : chunks_) {
        // THE key line of camera-relative rendering: the big numbers cancel out in double
        // precision on the CPU, and the GPU only ever sees the small difference.
        const Vec3f offset(chunk.origin - cameraPos);
        shader_.set("uChunkOffset", offset);

        glBindVertexArray(chunk.vao.get());
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_SHORT, nullptr);
    }

    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

} // namespace orbis
