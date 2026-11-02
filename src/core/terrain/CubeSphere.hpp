#pragma once

#include "core/math/Vec3.hpp"

#include <array>
#include <cstdint>

namespace orbis {

/// The planet is a cube "inflated" into a sphere: 6 square faces, each a regular grid.
/// Unlike a lat/lon (UV) sphere there are no poles where triangles collapse into slivers, and
/// every face can later be split recursively into a quadtree (stage 2: LOD).
enum class CubeFace : std::uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

inline constexpr std::array<CubeFace, 6> kAllCubeFaces = {
    CubeFace::PosX, CubeFace::NegX, CubeFace::PosY,
    CubeFace::NegY, CubeFace::PosZ, CubeFace::NegZ,
};

/// Orientation of one face. Invariant: cross(uAxis, vAxis) == normal. Therefore a grid
/// walked in (+u, +v) order yields counter-clockwise triangles seen from OUTSIDE the planet -
/// the same winding on all 6 faces, so back-face culling just works.
struct FaceBasis {
    Vec3d normal;
    Vec3d uAxis;
    Vec3d vAxis;
};

const FaceBasis& faceBasis(CubeFace face);

/// Point on the cube surface for face coordinates u, v in [-1, 1].
Vec3d faceToCube(CubeFace face, double u, double v);

/// Maps a point on the unit cube surface onto the unit sphere.
///
/// Plain normalize(c) crowds vertices near face corners (cells there are ~5x smaller than in
/// the face centre). This "spherified cube" mapping is much more uniform and is still exact
/// on shared edges, because each output component depends only on the input point, never on
/// which face it came from.
Vec3d cubeToSphere(const Vec3d& c);

inline Vec3d faceToSphere(CubeFace face, double u, double v) {
    return cubeToSphere(faceToCube(face, u, v));
}

} // namespace orbis
