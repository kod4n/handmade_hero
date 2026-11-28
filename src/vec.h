#pragma once
#include "types.h"
#include <cmath>
#include <concepts>
#include <type_traits>

namespace hh {

// Vectors are templated on their component type; the f32 ones keep the plain
// names (Vec2, Vec3, Vec4) and the integer ones carry a suffix (Vec2u, Vec3i).
//
// Scalar parameters are std::type_identity_t<T> so they don't take part in
// deduction: T comes from the vector alone, and `v * 2` on a Vec2 converts the
// 2 instead of failing to deduce. Vectors of different component types don't
// mix; convert explicitly.

template <typename T>
struct TVec2 {
    T x;
    T y;
    // Every component set to v, e.g. Vec2::all(side) for a square.
    static constexpr TVec2 all(T v) {
        return {v, v};
    }
    constexpr bool operator==(const TVec2&) const = default;
};
template <typename T>
inline constexpr TVec2<T> operator+(TVec2<T> a, TVec2<T> b) {
    return {
        .x = a.x + b.x,
        .y = a.y + b.y,
    };
}
template <typename T>
inline constexpr TVec2<T> operator-(TVec2<T> a, TVec2<T> b) {
    return {
        .x = a.x - b.x,
        .y = a.y - b.y,
    };
}
template <typename T>
    requires std::is_signed_v<T>
inline constexpr TVec2<T> operator-(TVec2<T> a) {
    return {
        .x = -a.x,
        .y = -a.y,
    };
}
template <typename T>
inline constexpr TVec2<T> operator*(TVec2<T> v, std::type_identity_t<T> f) {
    return {
        .x = v.x * f,
        .y = v.y * f,
    };
}
template <typename T>
inline constexpr TVec2<T> operator*(std::type_identity_t<T> f, TVec2<T> v) {
    return v * f;
}
template <typename T>
inline constexpr TVec2<T>& operator*=(TVec2<T>& a, std::type_identity_t<T> f) {
    return a = a * f;
}
template <typename T>
inline constexpr TVec2<T>& operator+=(TVec2<T>& a, TVec2<T> b) {
    return a = a + b;
}
template <typename T>
inline constexpr TVec2<T>& operator-=(TVec2<T>& a, TVec2<T> b) {
    return a = a - b;
}
template <typename T>
inline constexpr T dot(TVec2<T> a, TVec2<T> b) {
    return a.x * b.x + a.y * b.y;
}
// Component-wise product, e.g. scaling each axis by a different amount.
template <typename T>
inline constexpr TVec2<T> hadamard(TVec2<T> a, TVec2<T> b) {
    return {
        .x = a.x * b.x,
        .y = a.y * b.y,
    };
}
template <typename T>
inline constexpr T length_sq(TVec2<T> v) {
    return dot(v, v);
}
template <std::floating_point T>
inline T length(TVec2<T> v) {
    return std::sqrt(length_sq(v));
}

template <typename T>
struct TVec3 {
    T x;
    T y;
    T z;
    // Every component set to v.
    static constexpr TVec3 all(T v) {
        return {v, v, v};
    }
    constexpr bool operator==(const TVec3&) const = default;
};
template <typename T>
inline constexpr TVec3<T> operator+(TVec3<T> a, TVec3<T> b) {
    return {
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
    };
}
template <typename T>
inline constexpr TVec3<T> operator-(TVec3<T> a, TVec3<T> b) {
    return {
        .x = a.x - b.x,
        .y = a.y - b.y,
        .z = a.z - b.z,
    };
}
template <typename T>
    requires std::is_signed_v<T>
inline constexpr TVec3<T> operator-(TVec3<T> a) {
    return {
        .x = -a.x,
        .y = -a.y,
        .z = -a.z,
    };
}
template <typename T>
inline constexpr TVec3<T> operator*(TVec3<T> v, std::type_identity_t<T> f) {
    return {
        .x = v.x * f,
        .y = v.y * f,
        .z = v.z * f,
    };
}
template <typename T>
inline constexpr TVec3<T> operator*(std::type_identity_t<T> f, TVec3<T> v) {
    return v * f;
}
template <typename T>
inline constexpr TVec3<T>& operator*=(TVec3<T>& a, std::type_identity_t<T> f) {
    return a = a * f;
}
template <typename T>
inline constexpr TVec3<T>& operator+=(TVec3<T>& a, TVec3<T> b) {
    return a = a + b;
}
template <typename T>
inline constexpr TVec3<T>& operator-=(TVec3<T>& a, TVec3<T> b) {
    return a = a - b;
}
template <typename T>
inline constexpr T dot(TVec3<T> a, TVec3<T> b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
template <typename T>
inline constexpr TVec3<T> hadamard(TVec3<T> a, TVec3<T> b) {
    return {
        .x = a.x * b.x,
        .y = a.y * b.y,
        .z = a.z * b.z,
    };
}
template <typename T>
inline constexpr T length_sq(TVec3<T> v) {
    return dot(v, v);
}
template <std::floating_point T>
inline T length(TVec3<T> v) {
    return std::sqrt(length_sq(v));
}

template <typename T>
struct TVec4 {
    T x;
    T y;
    T z;
    T w;
    // Every component set to v.
    static constexpr TVec4 all(T v) {
        return {v, v, v, v};
    }
    constexpr bool operator==(const TVec4&) const = default;
};
template <typename T>
inline constexpr TVec4<T> operator+(TVec4<T> a, TVec4<T> b) {
    return {
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
        .w = a.w + b.w,
    };
}
template <typename T>
inline constexpr TVec4<T> operator-(TVec4<T> a, TVec4<T> b) {
    return {
        .x = a.x - b.x,
        .y = a.y - b.y,
        .z = a.z - b.z,
        .w = a.w - b.w,
    };
}
template <typename T>
    requires std::is_signed_v<T>
inline constexpr TVec4<T> operator-(TVec4<T> a) {
    return {
        .x = -a.x,
        .y = -a.y,
        .z = -a.z,
        .w = -a.w,
    };
}
template <typename T>
inline constexpr TVec4<T> operator*(TVec4<T> v, std::type_identity_t<T> f) {
    return {
        .x = v.x * f,
        .y = v.y * f,
        .z = v.z * f,
        .w = v.w * f,
    };
}
template <typename T>
inline constexpr TVec4<T> operator*(std::type_identity_t<T> f, TVec4<T> v) {
    return v * f;
}
template <typename T>
inline constexpr TVec4<T>& operator*=(TVec4<T>& a, std::type_identity_t<T> f) {
    return a = a * f;
}
template <typename T>
inline constexpr TVec4<T>& operator+=(TVec4<T>& a, TVec4<T> b) {
    return a = a + b;
}
template <typename T>
inline constexpr TVec4<T>& operator-=(TVec4<T>& a, TVec4<T> b) {
    return a = a - b;
}
template <typename T>
inline constexpr T dot(TVec4<T> a, TVec4<T> b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
template <typename T>
inline constexpr TVec4<T> hadamard(TVec4<T> a, TVec4<T> b) {
    return {
        .x = a.x * b.x,
        .y = a.y * b.y,
        .z = a.z * b.z,
        .w = a.w * b.w,
    };
}
template <typename T>
inline constexpr T length_sq(TVec4<T> v) {
    return dot(v, v);
}
template <std::floating_point T>
inline T length(TVec4<T> v) {
    return std::sqrt(length_sq(v));
}

using Vec2 = TVec2<f32>;
using Vec3 = TVec3<f32>;
using Vec4 = TVec4<f32>;
using Vec2i = TVec2<i32>;
using Vec3i = TVec3<i32>;
using Vec4i = TVec4<i32>;
using Vec2u = TVec2<u32>;
using Vec3u = TVec3<u32>;
using Vec4u = TVec4<u32>;

inline constexpr f32 square(f32 v) {
    return v * v;
}

}  // namespace hh
