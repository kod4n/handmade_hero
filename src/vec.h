#pragma once
#include "types.h"
#include <cmath>

namespace hh {

struct Vec2 {
    f32 x;
    f32 y;
};
inline constexpr Vec2 operator+(Vec2 a, Vec2 b) {
    return {
        .x = a.x + b.x,
        .y = a.y + b.y,
    };
}
inline constexpr Vec2 operator-(Vec2 a, Vec2 b) {
    return {
        .x = a.x - b.x,
        .y = a.y - b.y,
    };
}
inline constexpr Vec2 operator-(Vec2 a) {
    return {
        .x = -a.x,
        .y = -a.y,
    };
}
inline constexpr Vec2 operator*(Vec2 v, f32 f) {
    return {
        .x = v.x * f,
        .y = v.y * f,
    };
}
inline constexpr Vec2 operator*(f32 f, Vec2 v) {
    return v * f;
}
inline constexpr Vec2& operator*=(Vec2& a, f32 f) {
    return a = a * f;
}
inline constexpr Vec2& operator+=(Vec2& a, Vec2 b) {
    return a = a + b;
}
inline constexpr Vec2& operator-=(Vec2& a, Vec2 b) {
    return a = a - b;
}
inline constexpr f32 dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}
// Component-wise product, e.g. scaling each axis by a different amount.
inline constexpr Vec2 hadamard(Vec2 a, Vec2 b) {
    return {
        .x = a.x * b.x,
        .y = a.y * b.y,
    };
}
inline constexpr f32 length_sq(Vec2 v) {
    return dot(v, v);
}
inline f32 length(Vec2 v) {
    return sqrtf(length_sq(v));
}

struct Vec3 {
    f32 x;
    f32 y;
    f32 z;
};
inline constexpr Vec3 operator+(Vec3 a, Vec3 b) {
    return {
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
    };
}
inline constexpr Vec3 operator-(Vec3 a, Vec3 b) {
    return {
        .x = a.x - b.x,
        .y = a.y - b.y,
        .z = a.z - b.z,
    };
}
inline constexpr Vec3 operator-(Vec3 a) {
    return {
        .x = -a.x,
        .y = -a.y,
        .z = -a.z,
    };
}
inline constexpr Vec3 operator*(Vec3 v, f32 f) {
    return {
        .x = v.x * f,
        .y = v.y * f,
        .z = v.z * f,
    };
}
inline constexpr Vec3 operator*(f32 f, Vec3 v) {
    return v * f;
}
inline constexpr Vec3& operator*=(Vec3& a, f32 f) {
    return a = a * f;
}
inline constexpr Vec3& operator+=(Vec3& a, Vec3 b) {
    return a = a + b;
}
inline constexpr Vec3& operator-=(Vec3& a, Vec3 b) {
    return a = a - b;
}
inline constexpr f32 dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline constexpr Vec3 hadamard(Vec3 a, Vec3 b) {
    return {
        .x = a.x * b.x,
        .y = a.y * b.y,
        .z = a.z * b.z,
    };
}
inline constexpr f32 length_sq(Vec3 v) {
    return dot(v, v);
}
inline f32 length(Vec3 v) {
    return sqrtf(length_sq(v));
}

struct Vec4 {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};
inline constexpr Vec4 operator+(Vec4 a, Vec4 b) {
    return {
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
        .w = a.w + b.w,
    };
}
inline constexpr Vec4 operator-(Vec4 a, Vec4 b) {
    return {
        .x = a.x - b.x,
        .y = a.y - b.y,
        .z = a.z - b.z,
        .w = a.w - b.w,
    };
}
inline constexpr Vec4 operator-(Vec4 a) {
    return {
        .x = -a.x,
        .y = -a.y,
        .z = -a.z,
        .w = -a.w,
    };
}
inline constexpr Vec4 operator*(Vec4 v, f32 f) {
    return {
        .x = v.x * f,
        .y = v.y * f,
        .z = v.z * f,
        .w = v.w * f,
    };
}
inline constexpr Vec4 operator*(f32 f, Vec4 v) {
    return v * f;
}
inline constexpr Vec4& operator*=(Vec4& a, f32 f) {
    return a = a * f;
}
inline constexpr Vec4& operator+=(Vec4& a, Vec4 b) {
    return a = a + b;
}
inline constexpr Vec4& operator-=(Vec4& a, Vec4 b) {
    return a = a - b;
}
inline constexpr f32 dot(Vec4 a, Vec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
inline constexpr Vec4 hadamard(Vec4 a, Vec4 b) {
    return {
        .x = a.x * b.x,
        .y = a.y * b.y,
        .z = a.z * b.z,
        .w = a.w * b.w,
    };
}
inline constexpr f32 length_sq(Vec4 v) {
    return dot(v, v);
}
inline f32 length(Vec4 v) {
    return sqrtf(length_sq(v));
}

inline constexpr f32 square(f32 v) {
    return v * v;
}

}  // namespace hh
