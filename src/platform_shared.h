#pragma once

// Helpers shared by the platform layers (win32, macOS, Linux). The game never
// sees these: it only includes platform.h, the platform/game contract.

#include "platform.h"  // IWYU pragma: export

namespace hh {

// The game always renders at this size; the platform scales it for display.
inline constexpr i32 k_backbuffer_width = 960;
inline constexpr i32 k_backbuffer_height = 540;

// Black border around the image in a normal window, so the buffer's edges
// are visible.
inline constexpr i32 k_window_gutter_x = 10;
inline constexpr i32 k_window_gutter_y = 10;

// Where the buffer lands in the window's drawable area, top-left origin.
struct DisplayRect {
    i32 x;
    i32 y;
    i32 width;
    i32 height;
};

// In a normal window the buffer is drawn 1:1 inside the gutter, so every
// pixel on screen is exactly one the game wrote. Once the window can hold the
// buffer at 2x or more (fullscreen, or maximized), it's scaled up to fill as
// much as it can without changing the aspect ratio, and centered.
inline DisplayRect display_rect(i32 buffer_width, i32 buffer_height, i32 window_width, i32 window_height) {
    if (window_width < 2 * buffer_width || window_height < 2 * buffer_height) {
        return {
            .x = k_window_gutter_x,
            .y = k_window_gutter_y,
            .width = buffer_width,
            .height = buffer_height,
        };
    }

    // Fit the side that runs out first. Cross-multiplied to compare the
    // aspect ratios without dividing; i64 so the products can't overflow.
    DisplayRect rect = {};
    if (static_cast<i64>(window_width) * buffer_height <= static_cast<i64>(window_height) * buffer_width) {
        rect.width = window_width;
        rect.height = static_cast<i32>(static_cast<i64>(window_width) * buffer_height / buffer_width);
    } else {
        rect.height = window_height;
        rect.width = static_cast<i32>(static_cast<i64>(window_height) * buffer_width / buffer_height);
    }
    rect.x = (window_width - rect.width) / 2;
    rect.y = (window_height - rect.height) / 2;
    return rect;
}

inline u32 safe_truncate(u64 value) {
    assert(value <= UINT32_MAX);
    return static_cast<u32>(value);
}

}  // namespace hh
