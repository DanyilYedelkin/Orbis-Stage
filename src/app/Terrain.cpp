#include "app/Terrain.hpp"

#include "EmbeddedShaders.hpp"
#include "app/gl/ShaderSource.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace orbis {

namespace {
constexpr std::size_t kMaxResidentChunks = 3000;   // ~100 MB of vertex data at 32 quads/edge
constexpr std::uint64_t kEvictAfterFrames = 600;   // ~10 s unused at 60 fps
} // namespace

Terrain::Terrain(const Ellipsoid& ellipsoid, const ElevationSource& elevation,
                 const LodSettings& settings, const Atmosphere& atmosphere)
    : ellipsoid_(ellipsoid), lod_(ellipsoid, settings),
      shader_(shaders::terrain_vert,
              gl::composeShader(std::string(shaders::common_glsl) + atmosphere.shaderPrelude(),
                                shaders::terrain_frag),
              "terrain"),
      indexBuffer_(gl::createBuffer()),
      // The lambda captures the ellipsoid by value and the elevation source by reference:
      // sources are immutable after construction, so concurrent reads are safe.
      builder_(
          [ellipsoid, &elevation, n = settings.quadsPerEdge](const NodeKey& key) {
              return buildChunkMesh(key.region(), n, ellipsoid, elevation);
          },
          ChunkBuilder::defaultThreadCount()) {
    shader_.set("uTransmittanceLut", 0);
    shader_.set("uMultiScatteringLut", 1);
    const std::vector<std::uint16_t> indices = buildChunkIndices(settings.quadsPerEdge);
    indexCount_ = static_cast<GLsizei>(indices.size());
    trianglesPerChunk_ = indices.size() / 3;
    // Immutable storage (flags = 0): the data never changes, the driver can place it optimally.
    glNamedBufferStorage(indexBuffer_.get(),
                         static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint16_t)),
                         indices.data(), 0);
}

void Terrain::upload(const NodeKey& key, const ChunkMesh& mesh) {
    GpuChunk chunk{gl::createVertexArray(), gl::createBuffer(), mesh.origin,
                   HeightRange{mesh.minHeight, mesh.maxHeight}, frame_, key.level};
    glNamedBufferStorage(chunk.vertices.get(),
                         static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(TerrainVertex)),
                         mesh.vertices.data(), 0);

    // Vertex layout, described once per VAO with DSA (no glBind* juggling).
    const GLuint vao = chunk.vao.get();
    glVertexArrayVertexBuffer(vao, 0, chunk.vertices.get(), 0, sizeof(TerrainVertex));
    glVertexArrayElementBuffer(vao, indexBuffer_.get());

    const auto attribute = [vao](GLuint index, GLint components, GLenum type, GLboolean normalized,
                                 GLuint offset) {
        glEnableVertexArrayAttrib(vao, index);
        glVertexArrayAttribFormat(vao, index, components, type, normalized, offset);
        glVertexArrayAttribBinding(vao, index, 0);
    };
    attribute(0, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, position));
    attribute(1, 3, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, morphDelta));
    // Packed normals: the GPU unpacks 10:10:10:2 signed-normalised integers for free.
    attribute(2, 4, GL_INT_2_10_10_10_REV, GL_TRUE, offsetof(TerrainVertex, normal));
    attribute(3, 4, GL_INT_2_10_10_10_REV, GL_TRUE, offsetof(TerrainVertex, morphNormal));
    attribute(4, 2, GL_FLOAT, GL_FALSE, offsetof(TerrainVertex, elevation)); // + morphElevation

    chunks_.insert_or_assign(key, std::move(chunk));
}

void Terrain::update(const Camera& lodCamera, int viewportWidth, int viewportHeight,
                     std::size_t maxUploads) {
    ++frame_;

    // 1. Collect finished work.
    for (auto& [key, mesh] : builder_.takeCompleted(maxUploads)) {
        upload(key, mesh);
    }

    // 2. Select.
    const CameraBasis b = lodCamera.basis();
    const double fov = lodCamera.verticalFov;
    const double aspect = static_cast<double>(viewportWidth) / std::max(viewportHeight, 1);
    const LodView view{
        .cameraPosition = lodCamera.position(),
        .frustum = Frustum::fromCamera(b.forward, b.right, b.up, fov, aspect, lodCamera.nearPlane),
        .pixelsPerRadian = viewportHeight / (2.0 * std::tan(fov / 2.0)),
        .occluder = ellipsoid_,
    };
    lodCameraPosition_ = view.cameraPosition;
    pixelsPerRadian_ = view.pixelsPerRadian;
    LodSelection selection = lod_.select(view, [this](const NodeKey& key) {
        const auto it = chunks_.find(key);
        return it != chunks_.end() ? std::optional(it->second.heights) : std::nullopt;
    });

    for (const NodeKey& key : selection.touched) {
        chunks_.at(key).lastUsedFrame = frame_;
    }

    // 3. Request what is missing.
    lastSelectionComplete_ = selection.requests.empty();
    builder_.schedule(std::move(selection.requests));

    // Front-to-back order: nearer chunks fill the depth buffer first, so the GPU can reject
    // hidden fragments of farther chunks before shading them (early-Z).
    drawList_ = std::move(selection.draw);
    const Vec3d eye = lodCamera.position();
    std::sort(drawList_.begin(), drawList_.end(), [this, &eye](const NodeKey& a, const NodeKey& c) {
        return lengthSquared(chunks_.at(a).origin - eye) < lengthSquared(chunks_.at(c).origin - eye);
    });

    // 4. Bound memory.
    evict();

    stats_ = {
        .drawnChunks = drawList_.size(),
        .residentChunks = chunks_.size(),
        .pendingBuilds = builder_.pendingCount(),
        .runningBuilds = builder_.runningCount(),
        .drawnTriangles = drawList_.size() * trianglesPerChunk_,
        .deepestLevel = selection.deepestLevel,
        .visited = selection.visited,
        .frustumCulled = selection.frustumCulled,
        .horizonCulled = selection.horizonCulled,
    };
}

void Terrain::evict() {
    // Time-based: anything unused for a while goes, except the 6 roots (always needed as a
    // fallback). Size-based: above the budget, evict least recently used first. Chunks used
    // THIS frame are never evicted, so the current image cannot lose a chunk it draws.
    std::erase_if(chunks_, [this](const auto& entry) {
        const GpuChunk& c = entry.second;
        return c.level > 0 && frame_ - c.lastUsedFrame > kEvictAfterFrames;
    });

    if (chunks_.size() <= kMaxResidentChunks) {
        return;
    }
    std::vector<std::pair<std::uint64_t, NodeKey>> candidates;
    for (const auto& [key, chunk] : chunks_) {
        if (chunk.level > 0 && chunk.lastUsedFrame != frame_) {
            candidates.emplace_back(chunk.lastUsedFrame, key);
        }
    }
    const std::size_t excess = std::min(chunks_.size() - kMaxResidentChunks, candidates.size());
    std::nth_element(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(excess),
                     candidates.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t i = 0; i < excess; ++i) {
        chunks_.erase(candidates[i].second);
    }
}

bool Terrain::settled() const { return lastSelectionComplete_ && builder_.idle(); }

void Terrain::draw(const Camera& camera, float aspect, const TerrainDrawSettings& settings) const {
    const Vec3d& cameraPos = camera.position();
    const Mat4f viewProjection = camera.projectionMatrix(aspect) * camera.viewRotationMatrix();

    shader_.use();
    shader_.set("uViewProjection", viewProjection);
    shader_.set("uSunDirection", Vec3f(normalize(settings.sunDirection)));
    shader_.set("uPlanetCenter", Vec3f(-cameraPos)); // only used as a direction, float is fine
    shader_.set("uWireframe", settings.wireframe ? 1 : 0);
    shader_.set("uShowLodLevels", settings.showLodLevels ? 1 : 0);
    shader_.set("uMorphEnabled", settings.geomorph ? 1 : 0);
    shader_.set("uSunIlluminance", settings.sunIlluminance);

    glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

    for (const NodeKey& key : drawList_) {
        const auto it = chunks_.find(key);
        if (it == chunks_.end()) {
            continue;
        }
        const GpuChunk& chunk = it->second;
        // THE key line of camera-relative rendering: the big numbers cancel out in double
        // precision on the CPU, and the GPU only ever sees the small difference.
        shader_.set("uChunkOffset", Vec3f(chunk.origin - cameraPos));
        shader_.set("uLodOffset", Vec3f(chunk.origin - lodCameraPosition_));
        shader_.set("uLodLevel", static_cast<int>(chunk.level));

        // Morph window. The PARENT was split when its vertex spacing projected to more than
        // maxPixelError pixels, i.e. closer than D = parentSpacing * pixelsPerRadian / error.
        // So this chunk only exists closer than ~D: at D it must look exactly like the parent
        // (morph = 1), and by D/2 - where it splits into its own children - it must show its
        // own surface (morph = 0), so its children can in turn start from it. A narrow window
        // near D keeps most of the band at full detail (a wide one blurs the image by about
        // half a level); ending below D keeps borders next to coarser neighbours crack-free.
        if (chunk.level == 0) {
            shader_.set("uMorphRange", 1e30f, 2e30f); // roots have no parent
        } else {
            const NodeKey probe{CubeFace::PosX, chunk.level, 0, 0};
            const double parentSpacing = 2.0 * lod_.vertexSpacing(probe);
            const double d = parentSpacing * pixelsPerRadian_ / lod_.settings().maxPixelError;
            shader_.set("uMorphRange", static_cast<float>(0.70 * d), static_cast<float>(0.98 * d));
        }

        glBindVertexArray(chunk.vao.get());
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_SHORT, nullptr);
    }

    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

} // namespace orbis
