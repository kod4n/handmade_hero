#include "types.h"
#include "game.h"

static u32 pack_color(u8 r, u8 g, u8 b, u8 a = 0) {
    return (static_cast<u32>(a) << 24 | static_cast<u32>(r) << 16 | static_cast<u32>(g) << 8 | static_cast<u32>(b));
}

static void render_gradient(GameOffscreenBuffer& buffer, int green_offset, int blue_offset) {
    u8* row = static_cast<u8*>(buffer.memory);
    for (int y = 0; y < buffer.height; y++) {
        u32* pixel = reinterpret_cast<u32*>(row);
        for (int x = 0; x < buffer.width; x++) {
            *pixel++ = pack_color(0, x + green_offset, y + blue_offset);
        }
        row += buffer.pitch;
    }
}

static void game_render(GameOffscreenBuffer& buffer, int green_offset, int blue_offset) {
    render_gradient(buffer, green_offset, blue_offset);
}
