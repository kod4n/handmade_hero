#pragma once
#include <bit>
#include <cmath>
#include "types.h"

namespace hh {

struct BitscanResult {
    bool found;
    u32 index;
};

// std::rotl/rotr compile to rol/ror and treat a negative amount as a rotate
// the other way; unlike _rotl they also exist on the Linux/Mac builds.
inline u32 rotate_left(u32 value, i32 amount) {
    u32 res = std::rotl(value, amount);
    return res;
}

inline u32 rotate_right(u32 value, i32 amount) {
    u32 res = std::rotr(value, amount);
    return res;
}

// -1, 0 or 1.
inline constexpr i32 sign(i32 value) {
    return (value > 0) - (value < 0);
}

inline f32 sin(f32 angle) {
    f32 res = sinf(angle);
    return res;
}

inline f32 cos(f32 angle) {
    f32 res = cosf(angle);
    return res;
}

inline f32 atan2(f32 x, f32 y) {
    f32 res = atan2f(y, x);
    return res;
}

// Index of the lowest set bit. countr_zero compiles to tzcnt/bsf on every
// compiler we use, so no per-compiler intrinsics are needed.
inline BitscanResult bitscan_forward(u32 value) {
    BitscanResult res = {};
    if (value != 0) {
        res.found = true;
        res.index = static_cast<u32>(std::countr_zero(value));
    }
    return res;
}

}  // namespace hh
