#pragma once
#include "types.h"

namespace hh {

#pragma pack(push, 1)
struct BitmapHeader {
    u16 file_type;
    u32 file_size;
    u16 reserved1;
    u16 reserved2;
    u32 offset;
    u32 size;
    i32 width;
    i32 height;
    u16 planes;
    u16 bits_per_pixel;
    u32 compression;
    u32 size_of_bitmap;
    i32 horz_resolution;
    i32 vert_resolution;
    u32 colors_used;
    u32 colors_important;
    // Only meaningful when compression is BI_BITFIELDS (3).
    u32 red_mask;
    u32 green_mask;
    u32 blue_mask;
};
#pragma pack(pop)
static_assert(sizeof(BitmapHeader) == 66);

}  // namespace hh
