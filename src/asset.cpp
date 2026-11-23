#pragma once
#include "arena.h"
#include "asset.h"
#include "defer.h"
#include "intrinsics.h"
#include "platform.h"
#include "render.h"
#include <cstring>
#include <span>

namespace hh {

#if DEV
// Copies the pixels into the arena as top-down rows, then frees the file. The
// file's pixel data can start at an offset that isn't 4-aligned, so it's only
// read through memcpy; the copy is aligned and can be indexed directly.
LoadedBitmap load_bmp(
    Arena& arena,
    DevPlatformReadFile* read_file,
    DevPlatformFreeFile* free_file,
    const char* filename
) {
    LoadedBitmap result = {};
    ReadFileResult res = read_file(filename);
    if (!res.content) {
        return result;
    }
    defer(free_file(res.content));
    if (res.size < sizeof(BitmapHeader)) {
        return result;
    }

    const u8* content = static_cast<const u8*>(res.content);
    const BitmapHeader* header = reinterpret_cast<const BitmapHeader*>(content);
    if (header->file_type != 0x4D42 || header->bits_per_pixel != 32 || header->width <= 0 || header->height == 0) {
        return result;
    }

    // Positive height means rows are stored bottom-up.
    const bool bottom_up = header->height > 0;
    const u32 width = static_cast<u32>(header->width);
    const u32 height = bottom_up ? static_cast<u32>(header->height) : 0u - static_cast<u32>(header->height);
    const u64 row_bytes = static_cast<u64>(width) * sizeof(u32);
    // The header's dimensions come from the file, so check they fit in it.
    if (header->offset > res.size || row_bytes * height > res.size - header->offset) {
        return result;
    }

    // BI_BITFIELDS (3) stores the channel masks after the info header; BI_RGB (0)
    // has no masks and is always 0x00RRGGBB. Anything else is unsupported.
    constexpr u32 k_bi_rgb = 0;
    constexpr u32 k_bi_bitfields = 3;
    u32 red_mask = 0x00FF0000;
    u32 green_mask = 0x0000FF00;
    u32 blue_mask = 0x000000FF;
    if (header->compression == k_bi_bitfields) {
        red_mask = header->red_mask;
        green_mask = header->green_mask;
        blue_mask = header->blue_mask;
    } else if (header->compression != k_bi_rgb) {
        return result;
    }
    // Alpha isn't in the header (the V4 alpha mask is optional), so take
    // whatever bits the color channels don't use.
    const u32 alpha_mask = ~(red_mask | green_mask | blue_mask);

    const BitscanResult red_shift = bitscan_forward(red_mask);
    const BitscanResult green_shift = bitscan_forward(green_mask);
    const BitscanResult blue_shift = bitscan_forward(blue_mask);
    const BitscanResult alpha_shift = bitscan_forward(alpha_mask);
    if (!red_shift.found || !green_shift.found || !blue_shift.found) {
        return result;
    }

    std::span<u32> pixels = arena.push_array<u32>(static_cast<u64>(width) * height);
    const u8* src = content + header->offset;
    for (u32 y = 0; y < height; y++) {
        const u32 src_y = bottom_up ? height - 1 - y : y;
        u32* row = &pixels[static_cast<u64>(y) * width];
        std::memcpy(row, src + src_y * row_bytes, row_bytes);
        for (u32 x = 0; x < width; x++) {
            const u32 c = row[x];
            const u8 r = static_cast<u8>(c >> red_shift.index);
            const u8 g = static_cast<u8>(c >> green_shift.index);
            const u8 b = static_cast<u8>(c >> blue_shift.index);
            const u8 a = alpha_shift.found ? static_cast<u8>(c >> alpha_shift.index) : 0xFF;
            row[x] = pack_color(r, g, b, a);
        }
    }

    result.pixels = pixels.data();
    result.width = width;
    result.height = height;
    return result;
}
#endif

}  // namespace hh
