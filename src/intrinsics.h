#pragma once
#include <bit>  // IWYU pragma: keep (std::rotl etc. when not COMPILER_MSVC)
#include <cmath>
#include "types.h"
#if COMPILER_MSVC
#include <intrin.h>  // _rotl, _rotr, _BitScanForward
#endif

namespace hh {

struct BitscanResult {
    bool found;
    u32 index;
};

inline f32 abs(f32 value) {
    return fabsf(value);
}

// Negates in u32, where wrapping is defined, so INT32_MIN (which has no
// positive i32) comes back as INT32_MIN instead of overflowing.
inline constexpr i32 abs(i32 value) {
    return value < 0 ? static_cast<i32>(0u - static_cast<u32>(value)) : value;
}

// Both paths compile to rol/ror, and both treat a negative amount as a rotate
// the other way: std::rotl does by definition, and _rotl uses only the low 5
// bits of the amount, so -n rotates by 32 - n. std::rotl/rotr cover the
// Linux/Mac builds, which don't have _rotl.
inline u32 rotate_left(u32 value, i32 amount) {
#if COMPILER_MSVC
    u32 res = _rotl(value, amount);
#else
    u32 res = std::rotl(value, amount);
#endif
    return res;
}

inline u32 rotate_right(u32 value, i32 amount) {
#if COMPILER_MSVC
    u32 res = _rotr(value, amount);
#else
    u32 res = std::rotr(value, amount);
#endif
    return res;
}

// -1, 0 or 1.
inline constexpr i32 sign(i32 value) {
    return (value > 0) - (value < 0);
}

// a / b rounded toward negative infinity (b > 0), where C++'s / truncates
// toward zero: tile -1 is in room -1, not room 0.
inline constexpr i32 floor_div(i32 a, i32 b) {
    const i32 q = a / b;
    return (a % b < 0) ? q - 1 : q;
}

inline f32 sin(f32 angle) {
    f32 res = sinf(angle);
    return res;
}

inline f32 cos(f32 angle) {
    f32 res = cosf(angle);
    return res;
}

inline f32 atan2(f32 y, f32 x) {
    f32 res = atan2f(y, x);
    return res;
}

// Index of the lowest set bit. Both paths compile to bsf/tzcnt.
inline BitscanResult bitscan_forward(u32 value) {
    BitscanResult res = {};
#if COMPILER_MSVC
    // Takes unsigned long, which is 32 bits on Windows but a different type
    // from u32 (unsigned int), so it can't point at res.index directly. The
    // index is undefined when nothing is set, so it's only copied on a hit.
    unsigned long index = 0;
    if (_BitScanForward(&index, value)) {
        res.found = true;
        res.index = static_cast<u32>(index);
    }
#else
    if (value != 0) {
        res.found = true;
        res.index = static_cast<u32>(std::countr_zero(value));
    }
#endif
    return res;
}

}  // namespace hh
