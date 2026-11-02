#pragma once

#include <cmath>
#include <type_traits>

namespace orbis {

/// Minimal 3D vector.
///
/// Templated so the same code serves two very different jobs:
///   Vec3d (double) - world positions in ECEF space, where values reach ~6.4e6 m and we need
///                    sub-millimetre precision;
///   Vec3f (float)  - data sent to the GPU, always kept SMALL (relative to a nearby origin).
template <typename T>
struct Vec3 {
    static_assert(std::is_floating_point_v<T>, "Vec3 is meant for float or double");

    T x{};
    T y{};
    T z{};

    constexpr Vec3() = default;
    constexpr Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    /// Conversion between precisions. Explicit on purpose: dropping from double to float is
    /// exactly the operation that causes jitter at planetary scale, so it must be visible.
    template <typename U>
    constexpr explicit Vec3(const Vec3<U>& o)
        : x(static_cast<T>(o.x)), y(static_cast<T>(o.y)), z(static_cast<T>(o.z)) {}

    constexpr Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vec3& operator*=(T s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    constexpr Vec3& operator/=(T s) {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }
};

// std::type_identity_t blocks template deduction on the scalar, so `v * 2.0` works for Vec3d
// and `v * 2.0f` for Vec3f without "deduced conflicting types" errors.
template <typename T>
constexpr Vec3<T> operator+(Vec3<T> a, const Vec3<T>& b) { return a += b; }
template <typename T>
constexpr Vec3<T> operator-(Vec3<T> a, const Vec3<T>& b) { return a -= b; }
template <typename T>
constexpr Vec3<T> operator-(const Vec3<T>& v) { return {-v.x, -v.y, -v.z}; }
template <typename T>
constexpr Vec3<T> operator*(Vec3<T> v, std::type_identity_t<T> s) { return v *= s; }
template <typename T>
constexpr Vec3<T> operator*(std::type_identity_t<T> s, Vec3<T> v) { return v *= s; }
template <typename T>
constexpr Vec3<T> operator/(Vec3<T> v, std::type_identity_t<T> s) { return v /= s; }

template <typename T>
constexpr T dot(const Vec3<T>& a, const Vec3<T>& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// Right-handed cross product: cross(X, Y) == Z.
template <typename T>
constexpr Vec3<T> cross(const Vec3<T>& a, const Vec3<T>& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

template <typename T>
constexpr T lengthSquared(const Vec3<T>& v) { return dot(v, v); }

template <typename T>
T length(const Vec3<T>& v) { return std::sqrt(lengthSquared(v)); }

template <typename T>
T distance(const Vec3<T>& a, const Vec3<T>& b) { return length(a - b); }

/// Unit vector in the direction of v. Returns the zero vector for a zero input instead of NaNs.
template <typename T>
Vec3<T> normalize(const Vec3<T>& v) {
    const T len = length(v);
    return len > T(0) ? v / len : Vec3<T>{};
}

using Vec3d = Vec3<double>;
using Vec3f = Vec3<float>;

} // namespace orbis
