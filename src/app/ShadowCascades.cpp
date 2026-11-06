#include "app/ShadowCascades.hpp"

#include "EmbeddedShaders.hpp"
#include "app/Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace orbis {

namespace {
constexpr double kCasterExtent = 150000.0; // metres towards the sun: long shadows at low sun
}

ShadowCascades::ShadowCascades()
    : program_(shaders::terrain_vert, shaders::shadow_frag, "shadow") {
    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &id);
    depth_ = gl::Texture{id};
    glTextureStorage3D(id, 1, GL_DEPTH_COMPONENT32F, kResolution, kResolution, kCascades);
    glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Comparison mode with linear filtering: free 2x2 PCF.
    glTextureParameteri(id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(id, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    for (int i = 0; i < kCascades; ++i) {
        GLuint fbo = 0;
        glCreateFramebuffers(1, &fbo);
        framebuffers_[static_cast<std::size_t>(i)] = gl::Framebuffer{fbo};
        glNamedFramebufferTextureLayer(fbo, GL_DEPTH_ATTACHMENT, id, 0, i);
        glNamedFramebufferDrawBuffer(fbo, GL_NONE);
        glNamedFramebufferReadBuffer(fbo, GL_NONE);
        if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("ShadowCascades: framebuffer incomplete");
        }
    }
}

void ShadowCascades::render(const Camera& camera, float aspect, const Vec3d& sunDirection,
                            const Terrain& terrain, bool geomorph, double groundHeight) {
    const CameraBasis b = camera.basis();
    const Vec3d sun = normalize(sunDirection);

    // World-fixed light basis: rotating it with the camera would shimmer.
    const Vec3d helper = std::abs(sun.z) < 0.9 ? Vec3d{0, 0, 1} : Vec3d{1, 0, 0};
    const Vec3d lightRight = normalize(cross(helper, sun));
    const Vec3d lightUp = cross(sun, lightRight);
    const Vec3d lightForward = -sun;

    // Split distances grow geometrically and with height above the ground: at 150 m in a
    // valley the first cascade covers ~1.5 km, at 6 km altitude ~18 km.
    const double height = std::max(b.geodetic.height - groundHeight, 50.0);
    const double first = std::max(1500.0, 3.0 * height);
    const std::array<double, kCascades + 1> bounds = {static_cast<double>(camera.nearPlane), first,
                                                      first * 5.0, first * 25.0};

    const double tanY = std::tan(static_cast<double>(camera.verticalFov) / 2.0);
    const double tanX = tanY * static_cast<double>(aspect);

    for (int c = 0; c < kCascades; ++c) {
        const auto ci = static_cast<std::size_t>(c);
        Vec3d center{};
        std::array<Vec3d, 8> corners;
        for (int k = 0; k < 8; ++k) {
            const double d = bounds[(k & 4) ? ci + 1 : ci];
            const double sx = (k & 1) ? 1.0 : -1.0;
            const double sy = (k & 2) ? 1.0 : -1.0;
            corners[static_cast<std::size_t>(k)] =
                (b.forward + b.right * (sx * tanX) + b.up * (sy * tanY)) * d;
            center += corners[static_cast<std::size_t>(k)] / 8.0;
        }
        double radius = 0.0;
        for (const Vec3d& p : corners) {
            radius = std::max(radius, distance(p, center));
        }
        radius = std::ceil(radius); // constant per cascade => constant texel size

        // Snap in ECEF: camera-relative coordinates shift by fractional texels as the camera
        // moves.
        const double texel = 2.0 * radius / kResolution;
        const Vec3d worldCenter = camera.position() + center;
        const double cx = std::round(dot(worldCenter, lightRight) / texel) * texel;
        const double cy = std::round(dot(worldCenter, lightUp) / texel) * texel;
        const double cz = dot(worldCenter, sun);
        const Vec3d snapped = lightRight * cx + lightUp * cy + sun * cz - camera.position();

        const Vec3d eye = snapped + sun * (radius + kCasterExtent);
        const Mat4f view = viewMatrix(Vec3f(eye), Vec3f(lightRight), Vec3f(lightUp),
                                      Vec3f(lightForward));
        const Mat4f projection = orthographicZeroToOne(
            static_cast<float>(radius), static_cast<float>(radius), 0.0f,
            static_cast<float>(2.0 * radius + kCasterExtent));
        lightMatrices_[ci] = projection * view;
        splits_[ci] = static_cast<float>(bounds[ci + 1]);
        texelSizes_[ci] = static_cast<float>(texel);

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffers_[ci].get());
        glViewport(0, 0, kResolution, kResolution);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);
        terrain.drawDepth(program_, camera, lightMatrices_[ci], geomorph);
    }
}

void ShadowCascades::bind() const { glBindTextureUnit(2, depth_.get()); }

} // namespace orbis
