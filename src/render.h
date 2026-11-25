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

// Bit position of each channel in a 0xAARRGGBB pixel.
inline constexpr u32 k_alpha_shift = 24;
inline constexpr u32 k_red_shift = 16;
inline constexpr u32 k_green_shift = 8;
inline constexpr u32 k_blue_shift = 0;

// Combines channels that are already at their bit positions (k_*_shift), e.g.
// masked out of another pixel. For plain 0..255 values use pack_color.
inline constexpr u32 pack_shifted_color(u32 r, u32 g, u32 b, u32 a = 0) {
    return a | r | g | b;
}

inline constexpr u32 pack_color(u8 r, u8 g, u8 b, u8 a = 0) {
    return pack_shifted_color(
        static_cast<u32>(r) << k_red_shift,
        static_cast<u32>(g) << k_green_shift,
        static_cast<u32>(b) << k_blue_shift,
        static_cast<u32>(a) << k_alpha_shift
    );
}

}  // namespace hh
