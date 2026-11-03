#pragma once

#include "core/math/Vec3.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace orbis {

template <typename T>
struct Vec4 {
    T x{};
    T y{};
    T z{};
    T w{};
};

/// 4x4 matrix stored column-major - the same memory layout as GLSL `mat4` - so data() can be
/// uploaded with glUniformMatrix4fv(..., transpose = GL_FALSE). Element access is (column, row).
template <typename T>
class Mat4 {
public:
    constexpr Mat4() = default; ///< All zeros.

    static constexpr Mat4 identity() {
        Mat4 m;
        for (int i = 0; i < 4; ++i) {
            m(i, i) = T(1);
        }
        return m;
    }

    constexpr T& operator()(int col, int row) { return e_[index(col, row)]; }
    constexpr T operator()(int col, int row) const { return e_[index(col, row)]; }

    constexpr const T* data() const { return e_.data(); }

private:
    static constexpr std::size_t index(int col, int row) {
        return static_cast<std::size_t>(col * 4 + row);
    }

    std::array<T, 16> e_{};
};

template <typename T>
constexpr Mat4<T> operator*(const Mat4<T>& a, const Mat4<T>& b) {
    Mat4<T> r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            T sum{};
            for (int k = 0; k < 4; ++k) {
                sum += a(k, row) * b(col, k);
            }
            r(col, row) = sum;
        }
    }
    return r;
}

template <typename T>
constexpr Vec4<T> operator*(const Mat4<T>& m, const Vec4<T>& v) {
    return {m(0, 0) * v.x + m(1, 0) * v.y + m(2, 0) * v.z + m(3, 0) * v.w,
            m(0, 1) * v.x + m(1, 1) * v.y + m(2, 1) * v.z + m(3, 1) * v.w,
            m(0, 2) * v.x + m(1, 2) * v.y + m(2, 2) * v.z + m(3, 2) * v.w,
            m(0, 3) * v.x + m(1, 3) * v.y + m(2, 3) * v.z + m(3, 3) * v.w};
}

/// Perspective projection with REVERSED Z and an INFINITE far plane, for a [0, 1] clip-space
/// depth range (glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE), glDepthFunc(GL_GREATER)).
///
/// Resulting depth is  zNear / -z_view:  1 at the near plane, -> 0 towards infinity.
/// Float numbers are densest near 0, and 1/z is densest near the camera; reversing Z makes the
/// two distributions cancel out, giving near-uniform precision from 0.5 m to the Moon.
template <typename T>
Mat4<T> perspectiveReversedZInfinite(T verticalFov, T aspect, T zNear) {
    const T f = T(1) / std::tan(verticalFov / T(2));
    Mat4<T> m;
    m(0, 0) = f / aspect;
    m(1, 1) = f;
    m(2, 3) = T(-1); // w_clip = -z_view
    m(3, 2) = zNear; // z_clip = zNear
    return m;
}

/// Rotation-only view matrix from an orthonormal camera basis (camera looks down -Z, OpenGL
/// convention). There is deliberately no translation: the renderer works in camera-relative
/// coordinates, so the camera is always at the origin.
template <typename T>
Mat4<T> viewRotation(const Vec3<T>& right, const Vec3<T>& up, const Vec3<T>& forward) {
    Mat4<T> m = Mat4<T>::identity();
    m(0, 0) = right.x;    m(1, 0) = right.y;    m(2, 0) = right.z;
    m(0, 1) = up.x;       m(1, 1) = up.y;       m(2, 1) = up.z;
    m(0, 2) = -forward.x; m(1, 2) = -forward.y; m(2, 2) = -forward.z;
    return m;
}

/// Orthographic projection for a view looking down -Z, depth mapped to [0, 1] (the clip control
/// used everywhere in the renderer). Standard depth here (near -> 0): shadow maps need no
/// reversed-Z, their range is short and linear.
template <typename T>
Mat4<T> orthographicZeroToOne(T halfWidth, T halfHeight, T zNear, T zFar) {
    Mat4<T> m = Mat4<T>::identity();
    m(0, 0) = T(1) / halfWidth;
    m(1, 1) = T(1) / halfHeight;
    m(2, 2) = T(-1) / (zFar - zNear);
    m(3, 2) = -zNear / (zFar - zNear);
    return m;
}

/// View matrix of a camera at `eye` with an orthonormal basis: rotation, then translation.
template <typename T>
Mat4<T> viewMatrix(const Vec3<T>& eye, const Vec3<T>& right, const Vec3<T>& up,
                   const Vec3<T>& forward) {
    Mat4<T> m = viewRotation(right, up, forward);
    m(3, 0) = -dot(right, eye);
    m(3, 1) = -dot(up, eye);
    m(3, 2) = dot(forward, eye);
    return m;
}

using Mat4f = Mat4<float>;
using Mat4d = Mat4<double>;

} // namespace orbis
