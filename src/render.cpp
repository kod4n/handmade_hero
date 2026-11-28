#pragma once
#include "platform.h"
#include "render.h"
#include <algorithm>
#include <cmath>
#include <span>

namespace hh {

// Fills [min, max) with color, clipped to the buffer. The fill is opaque:
// color.a isn't blended or written yet.
void render_rect(ScreenBuffer& buffer, Vec2 min, Vec2 max, Color color) {
    const f32 width = static_cast<int>(buffer.width);
    const f32 height = static_cast<int>(buffer.height);

    f32 x_min = round(min.x);
    f32 x_max = round(max.x);
    f32 y_min = round(min.y);
    f32 y_max = round(max.y);

    x_min = std::clamp(x_min, 0.0f, width);
    x_max = std::clamp(x_max, 0.0f, width);
    y_min = std::clamp(y_min, 0.0f, height);
    y_max = std::clamp(y_max, 0.0f, height);

    const u32 packed = pack_color(
        static_cast<u8>(round(color.r * 255.0f)),
        static_cast<u8>(round(color.g * 255.0f)),
        static_cast<u8>(round(color.b * 255.0f))
    );

    for (u32 y = y_min; y < y_max; y++) {
        std::span<u32> row = buffer.pixels.subspan(static_cast<u32>(y) * buffer.pitch, buffer.width);
        for (u32 x = x_min; x < x_max; x++) {
            row[static_cast<u32>(x)] = packed;
        }
    }
}

// creates a border using the provided thickness and color
// the min and max vectors are taken from the top left as origin
void render_rect_outline(ScreenBuffer& buffer, Vec2 min, Vec2 max, f32 thickness, Color color) {
    render_rect(buffer, min, {max.x, min.y + thickness}, color);
    render_rect(buffer, {min.x, max.y - thickness}, max, color);
    render_rect(buffer, {min.x, min.y + thickness}, {min.x + thickness, max.y - thickness}, color);
    render_rect(buffer, {max.x - thickness, min.y + thickness}, {max.x, max.y - thickness}, color);
}

// Stretches the bitmap over the whole buffer, nearest-neighbor.
void draw_bitmap_fullscreen(ScreenBuffer& buffer, const LoadedBitmap& bitmap) {
    if (!bitmap.pixels) {
        return;
    }

    for (u32 y = 0; y < buffer.height; y++) {
        const u32 src_y = static_cast<u32>(static_cast<u64>(y) * bitmap.height / buffer.height);
        const u32* src_row = bitmap.pixels + static_cast<u64>(src_y) * bitmap.width;
        std::span<u32> row = buffer.pixels.subspan(static_cast<u64>(y) * buffer.pitch, buffer.width);
        for (u32 x = 0; x < buffer.width; x++) {
            const u32 src_x = static_cast<u32>(static_cast<u64>(x) * bitmap.width / buffer.width);
            row[x] = src_row[src_x];
        }
    }
}

// Draws the bitmap 1:1 with its top-left corner at (x, y), clipped to the
// buffer. Blends by the bitmap's straight (non-premultiplied) alpha, in
// approximately linear space.
void draw_bitmap(ScreenBuffer& buffer, const LoadedBitmap& bitmap, Vec2 pos) {
    if (!bitmap.pixels) {
        return;
    }

    const i32 left = static_cast<i32>(roundf(pos.x));
    const i32 top = static_cast<i32>(roundf(pos.y));
    const i32 x_min = std::clamp(left, 0, static_cast<i32>(buffer.width));
    const i32 y_min = std::clamp(top, 0, static_cast<i32>(buffer.height));
    const i32 x_max = std::clamp(left + static_cast<i32>(bitmap.width), 0, static_cast<i32>(buffer.width));
    const i32 y_max = std::clamp(top + static_cast<i32>(bitmap.height), 0, static_cast<i32>(buffer.height));
    if (x_min >= x_max || y_min >= y_max) {
        return;
    }
    const u32 count = static_cast<u32>(x_max - x_min);

    for (i32 dst_y = y_min; dst_y < y_max; dst_y++) {
        const u32* src = bitmap.pixels + static_cast<u64>(dst_y - top) * bitmap.width + (x_min - left);
        std::span<u32> dst = buffer.pixels.subspan(static_cast<u64>(dst_y) * buffer.pitch + x_min, count);
        for (u32 i = 0; i < count; i++) {
            const u32 s = src[i];
            const u32 a = s >> 24;
            if (a == 255) {
                dst[i] = s;
            } else if (a != 0) {
                const u32 d = dst[i];
                const f32 t = static_cast<f32>(a) * k_inv_255;
                const f32 inv_t = 1.0f - t;
                // Blend in linear space, approximating sRGB as gamma 2: square
                // to linearize, lerp, sqrt back. Keeps translucent edges from
                // going dark the way a straight sRGB lerp does.
                auto to_linear = [](u32 c) {
                    const f32 v = static_cast<f32>(c) * k_inv_255;
                    return v * v;
                };
                auto blend = [&](u32 shift) {
                    const f32 sc = to_linear((s >> shift) & 0xFF);
                    const f32 dc = to_linear((d >> shift) & 0xFF);
                    const f32 lin = inv_t * dc + t * sc;
                    return static_cast<u32>(sqrtf(lin) * 255.0f + 0.5f) << shift;
                };
                dst[i] = blend(16) | blend(8) | blend(0);
            }
        }
    }
}

}  // namespace hh
