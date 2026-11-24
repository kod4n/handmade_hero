// Tests for src/render.cpp. Built as part of tests/main.cpp; the includes are here
// so clangd can make sense of this file on its own.

#include "test.h"
#include "render.cpp"

using namespace hh;

static_assert(Color{1.0f, 0.5f, 0.0f}.a == 1.0f);  // alpha defaults to opaque
static_assert(Color{.r = 1.0f, .g = 0.5f, .b = 0.0f, .a = 0.25f}.a == 0.25f);
static_assert([] {
    constexpr Color c = gray_value(0.75f);
    return c.r == 0.75f && c.g == 0.75f && c.b == 0.75f && c.a == 1.0f;
}());
static_assert(gray_value(0.0f).r == 0.0f && gray_value(1.0f).b == 1.0f);  // black and white ends

// An 8x8 buffer whose pitch is wider than its width, with a guard row above
// and below. Everything starts as k_sentinel; anything drawn outside the
// visible 8x8 shows up as a changed sentinel.
struct TestScreen {
    static constexpr u32 k_width = 8;
    static constexpr u32 k_height = 8;
    static constexpr u32 k_pitch = 10;
    static constexpr u32 k_guard = k_pitch;
    static constexpr u32 k_sentinel = 0xDEADBEEF;

    u32 storage[k_guard + k_pitch * k_height + k_guard];
    ScreenBuffer buffer;

    TestScreen() {
        for (u32& pixel : storage) {
            pixel = k_sentinel;
        }
        buffer = {
            .pixels = std::span<u32>(storage + k_guard, k_pitch * k_height),
            .width = k_width,
            .height = k_height,
            .pitch = k_pitch,
        };
    }

    u32 at(u32 x, u32 y) const {
        return storage[k_guard + y * k_pitch + x];
    }

    u32 written() const {
        u32 count = 0;
        for (u32 y = 0; y < k_height; y++) {
            for (u32 x = 0; x < k_width; x++) {
                count += at(x, y) != k_sentinel;
            }
        }
        return count;
    }

    bool outside_untouched() const {
        for (u32 i = 0; i < k_guard; i++) {
            if (storage[i] != k_sentinel || storage[k_guard + k_pitch * k_height + i] != k_sentinel) {
                return false;
            }
        }
        for (u32 y = 0; y < k_height; y++) {
            for (u32 x = k_width; x < k_pitch; x++) {
                if (at(x, y) != k_sentinel) {
                    return false;
                }
            }
        }
        return true;
    }
};

TEST(screen_buffer_size) {
    // Width and height, not pitch: the test screen's rows are 10 pixels apart.
    TestScreen screen;
    const Vec2 size = screen.buffer.size_vec();
    CHECK_EQ(size.x, 8.0f);
    CHECK_EQ(size.y, 8.0f);
}

TEST(render_rect_clips) {
    const u32 red = pack_color(255, 0, 0);

    TestScreen corner;
    render_rect(corner.buffer, {-5.0f, -5.0f}, {3.0f, 2.0f}, {1.0f, 0.0f, 0.0f});
    CHECK_EQ(corner.written(), 3 * 2);
    CHECK_EQ(corner.at(0, 0), red);
    CHECK_EQ(corner.at(2, 1), red);
    CHECK_EQ(corner.at(3, 0), TestScreen::k_sentinel);
    CHECK(corner.outside_untouched());

    TestScreen everything;
    render_rect(everything.buffer, {-100.0f, -100.0f}, {100.0f, 100.0f}, {1.0f, 0.0f, 0.0f});
    CHECK_EQ(everything.written(), 8 * 8);
    CHECK(everything.outside_untouched());

    TestScreen off_screen;
    render_rect(off_screen.buffer, {8.0f, 0.0f}, {20.0f, 8.0f}, {1.0f, 0.0f, 0.0f});
    render_rect(off_screen.buffer, {0.0f, -10.0f}, {8.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    CHECK_EQ(off_screen.written(), 0);
    CHECK(off_screen.outside_untouched());

    TestScreen inverted;
    render_rect(inverted.buffer, {5.0f, 5.0f}, {2.0f, 2.0f}, {1.0f, 0.0f, 0.0f});
    CHECK_EQ(inverted.written(), 0);

    // Alpha isn't blended yet: a translucent color still fills solid.
    TestScreen translucent;
    render_rect(translucent.buffer, {0.0f, 0.0f}, {1.0f, 1.0f}, {.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 0.25f});
    CHECK_EQ(translucent.at(0, 0), red);
}

TEST(render_rect_rounds_edges) {
    TestScreen screen;
    // x: round(0.4) = 0 to round(2.6) = 3. y: round(1.5) = 2 to round(3.49) = 3.
    render_rect(screen.buffer, {0.4f, 1.5f}, {2.6f, 3.49f}, {0.0f, 1.0f, 0.0f});
    CHECK_EQ(screen.written(), 3);
    CHECK_EQ(screen.at(0, 2), pack_color(0, 255, 0));
    CHECK_EQ(screen.at(2, 2), pack_color(0, 255, 0));
    CHECK_EQ(screen.at(3, 2), TestScreen::k_sentinel);
}

TEST(draw_bitmap_clips) {
    // Opaque 3x3; pixel i is 0xFF000000 + i + 1.
    u32 pixels[9];
    for (u32 i = 0; i < 9; i++) {
        pixels[i] = 0xFF000000 | (i + 1);
    }
    const LoadedBitmap bitmap = {.pixels = pixels, .width = 3, .height = 3};

    TestScreen top_left;
    draw_bitmap(top_left.buffer, bitmap, {-1.0f, -1.0f});
    CHECK_EQ(top_left.written(), 4);
    CHECK_EQ(top_left.at(0, 0), pixels[4]);
    CHECK_EQ(top_left.at(1, 0), pixels[5]);
    CHECK_EQ(top_left.at(1, 1), pixels[8]);
    CHECK(top_left.outside_untouched());

    // Would spill into the pitch padding and the guard row if unclipped.
    TestScreen bottom_right;
    draw_bitmap(bottom_right.buffer, bitmap, {7.0f, 7.0f});
    CHECK_EQ(bottom_right.written(), 1);
    CHECK_EQ(bottom_right.at(7, 7), pixels[0]);
    CHECK(bottom_right.outside_untouched());

    // 6.6 rounds to 7.
    TestScreen rounded;
    draw_bitmap(rounded.buffer, bitmap, {6.6f, 0.0f});
    CHECK_EQ(rounded.written(), 3);
    CHECK_EQ(rounded.at(7, 2), pixels[6]);

    TestScreen off_screen;
    draw_bitmap(off_screen.buffer, bitmap, {8.0f, 0.0f});
    draw_bitmap(off_screen.buffer, bitmap, {-3.0f, 0.0f});
    draw_bitmap(off_screen.buffer, bitmap, {0.0f, -3.0f});
    draw_bitmap(off_screen.buffer, bitmap, {0.0f, 8.0f});
    CHECK_EQ(off_screen.written(), 0);
    CHECK(off_screen.outside_untouched());

    TestScreen no_pixels;
    draw_bitmap(no_pixels.buffer, {}, {0.0f, 0.0f});
    CHECK_EQ(no_pixels.written(), 0);
}

TEST(draw_bitmap_blends_alpha) {
    TestScreen screen;
    render_rect(screen.buffer, {0.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 0.0f, 0.0f});

    u32 transparent = 0x00FFFFFF;
    draw_bitmap(screen.buffer, {.pixels = &transparent, .width = 1, .height = 1}, {0.0f, 0.0f});
    CHECK_EQ(screen.at(0, 0), 0);

    // Half-white over black, blended in gamma-2 space:
    // sqrt(128 / 255) * 255 + 0.5 = 181.17.
    u32 half_white = 0x80FFFFFF;
    draw_bitmap(screen.buffer, {.pixels = &half_white, .width = 1, .height = 1}, {1.0f, 0.0f});
    CHECK_EQ(screen.at(1, 0), pack_color(181, 181, 181));

    // Blending a color over itself leaves it unchanged.
    render_rect(screen.buffer, {2.0f, 0.0f}, {3.0f, 1.0f}, {200.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f});
    CHECK_EQ(screen.at(2, 0), pack_color(200, 100, 50));
    u32 same = pack_color(200, 100, 50, 0x40);
    draw_bitmap(screen.buffer, {.pixels = &same, .width = 1, .height = 1}, {2.0f, 0.0f});
    CHECK_EQ(screen.at(2, 0), pack_color(200, 100, 50));
}

TEST(draw_bitmap_fullscreen_scales) {
    u32 quad[4] = {1, 2, 3, 4};
    TestScreen screen;
    draw_bitmap_fullscreen(screen.buffer, {.pixels = quad, .width = 2, .height = 2});
    CHECK_EQ(screen.at(0, 0), 1);
    CHECK_EQ(screen.at(3, 3), 1);
    CHECK_EQ(screen.at(4, 3), 2);
    CHECK_EQ(screen.at(3, 4), 3);
    CHECK_EQ(screen.at(7, 7), 4);
    CHECK(screen.outside_untouched());

    // 3 doesn't divide 8: columns map 0-2, 3-5, 6-7.
    u32 row[3] = {10, 20, 30};
    TestScreen uneven;
    draw_bitmap_fullscreen(uneven.buffer, {.pixels = row, .width = 3, .height = 1});
    CHECK_EQ(uneven.at(2, 0), 10);
    CHECK_EQ(uneven.at(3, 0), 20);
    CHECK_EQ(uneven.at(5, 0), 20);
    CHECK_EQ(uneven.at(6, 0), 30);
    CHECK_EQ(uneven.at(7, 7), 30);
    CHECK(uneven.outside_untouched());
}
