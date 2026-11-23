#pragma once
#include "types.h"

namespace hh {

inline constexpr f32 k_render_min_x = -30.0f;
inline constexpr f32 k_render_min_y = -30.0f;
inline constexpr f32 k_inv_255 = 1.0f / 255.0f;

// Straight (not premultiplied) color, each channel 0..1. Alpha defaults to
// opaque, so {r, g, b} is a solid color.
struct Color {
    f32 r;
    f32 g;
    f32 b;
    f32 a = 1.0f;
};

inline constexpr Color gray_value(f32 v) {
    return {v, v, v};
}

struct LoadedBitmap {
    u32* pixels;  // 0xAARRGGBB, top-down rows, width * height, in the arena
    u32 width;
    u32 height;
};

inline constexpr u32 pack_color(u8 r, u8 g, u8 b, u8 a = 0) {
    return static_cast<u32>(a) << 24 | static_cast<u32>(r) << 16 | static_cast<u32>(g) << 8 | static_cast<u32>(b);
}

}  // namespace hh
