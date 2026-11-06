#pragma once

#include "core/math/Vec3.hpp"

#include <array>
#include <cstdint>

namespace orbis {

enum class CubeFace : std::uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

inline constexpr std::array<CubeFace, 6> kAllCubeFaces = {
    CubeFace::PosX, CubeFace::NegX, CubeFace::PosY,
    CubeFace::NegY, CubeFace::PosZ, CubeFace::NegZ,
};

/// cross(uAxis, vAxis) == normal: the same winding on every face.
struct FaceBasis {
    Vec3d normal;
    Vec3d uAxis;
    Vec3d vAxis;
};

const FaceBasis& faceBasis(CubeFace face);

/// u, v in [-1, 1].
Vec3d faceToCube(CubeFace face, double u, double v);

/// Spherified cube: far more uniform cells than normalize(c), and still exact on shared edges.
Vec3d cubeToSphere(const Vec3d& c);

inline Vec3d faceToSphere(CubeFace face, double u, double v) {
    return cubeToSphere(faceToCube(face, u, v));
}

} // namespace orbis
