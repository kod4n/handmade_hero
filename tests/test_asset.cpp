// Tests for src/asset.cpp. Built as part of tests/main.cpp; the includes are here
// so clangd can make sense of this file on its own.

#include "test.h"
#include "test_support.h"
#include "asset.cpp"
#include "render.h"

using namespace hh;

// A .bmp built in memory. The pixel data follows the 66-byte header directly,
// so it starts unaligned, like in files load_bmp has to handle.
struct FakeFile {
    u8 bytes[512];
    u32 size;

    BitmapHeader& header() {
        return *reinterpret_cast<BitmapHeader*>(bytes);
    }
};

static FakeFile make_bmp(
    i32 width,
    i32 height,
    std::span<const u32> pixels,
    u32 compression = 0,
    u32 red_mask = 0,
    u32 green_mask = 0,
    u32 blue_mask = 0
) {
    FakeFile file = {};
    file.size = static_cast<u32>(sizeof(BitmapHeader) + pixels.size_bytes());
    assert(file.size <= sizeof(file.bytes));
    BitmapHeader& h = file.header();
    h.file_type = 0x4D42;
    h.file_size = file.size;
    h.offset = sizeof(BitmapHeader);
    h.size = sizeof(BitmapHeader) - 14;
    h.width = width;
    h.height = height;
    h.planes = 1;
    h.bits_per_pixel = 32;
    h.compression = compression;
    h.size_of_bitmap = static_cast<u32>(pixels.size_bytes());
    h.red_mask = red_mask;
    h.green_mask = green_mask;
    h.blue_mask = blue_mask;
    std::memcpy(file.bytes + sizeof(BitmapHeader), pixels.data(), pixels.size_bytes());
    return file;
}

static FakeFile* g_fake_file;
static int g_fake_frees;

static ReadFileResult fake_read_file(const char*) {
    if (!g_fake_file) {
        return {};
    }
    return {.size = g_fake_file->size, .content = g_fake_file->bytes};
}

static void fake_free_file(void* memory) {
    CHECK(g_fake_file && memory == g_fake_file->bytes);
    g_fake_frees++;
}

static LoadedBitmap load_fake(Arena& arena, FakeFile* file) {
    g_fake_file = file;
    g_fake_frees = 0;
    return load_bmp(arena, fake_read_file, fake_free_file, "fake.bmp");
}

static const u32 k_2x2[4] = {0xFF112233, 0xFF445566, 0xFF778899, 0xFFAABBCC};

TEST(load_bmp_row_order) {
    Arena arena = make_test_arena();

    // Positive height: the first row in the file is the bottom one.
    FakeFile bottom_up = make_bmp(2, 2, k_2x2);
    LoadedBitmap bm = load_fake(arena, &bottom_up);
    CHECK(bm.pixels != nullptr);
    CHECK_EQ(bm.width, 2);
    CHECK_EQ(bm.height, 2);
    CHECK_EQ(bm.pixels[0], k_2x2[2]);
    CHECK_EQ(bm.pixels[1], k_2x2[3]);
    CHECK_EQ(bm.pixels[2], k_2x2[0]);
    CHECK_EQ(bm.pixels[3], k_2x2[1]);
    CHECK_EQ(g_fake_frees, 1);
    CHECK_EQ(reinterpret_cast<uintptr_t>(bm.pixels) % alignof(u32), 0);

    // Negative height: top-down, file order.
    FakeFile top_down = make_bmp(2, -2, k_2x2);
    bm = load_fake(arena, &top_down);
    CHECK(bm.pixels != nullptr);
    CHECK_EQ(bm.height, 2);
    CHECK_EQ(bm.pixels[0], k_2x2[0]);
    CHECK_EQ(bm.pixels[3], k_2x2[3]);
}

TEST(load_bmp_bitfield_masks) {
    Arena arena = make_test_arena();

    // ABGR: red in the low byte; alpha takes the unused top byte.
    const u32 abgr[1] = {0x80332211};
    FakeFile file = make_bmp(1, 1, abgr, 3, 0x000000FF, 0x0000FF00, 0x00FF0000);
    LoadedBitmap bm = load_fake(arena, &file);
    CHECK(bm.pixels != nullptr);
    CHECK_EQ(bm.pixels[0], pack_color(0x11, 0x22, 0x33, 0x80));

    // RGBA: alpha takes the unused bottom byte.
    const u32 rgba[1] = {0x11223344};
    file = make_bmp(1, 1, rgba, 3, 0xFF000000, 0x00FF0000, 0x0000FF00);
    bm = load_fake(arena, &file);
    CHECK(bm.pixels != nullptr);
    CHECK_EQ(bm.pixels[0], pack_color(0x11, 0x22, 0x33, 0x44));

    // The color masks use every bit, so there's no alpha: it's opaque.
    const u32 no_alpha[1] = {0xAB112233};
    file = make_bmp(1, 1, no_alpha, 3, 0xFFFF0000, 0x0000FF00, 0x000000FF);
    bm = load_fake(arena, &file);
    CHECK(bm.pixels != nullptr);
    CHECK_EQ(bm.pixels[0], pack_color(0x11, 0x22, 0x33, 0xFF));
}

TEST(load_bmp_rejects_bad_files) {
    Arena arena = make_test_arena();

    // Rejected means: no bitmap, nothing allocated, and the file still freed.
    auto rejects = [&](FakeFile file) {
        const u64 used = arena.used;
        LoadedBitmap bm = load_fake(arena, &file);
        return !bm.pixels && bm.width == 0 && bm.height == 0 && arena.used == used && g_fake_frees == 1;
    };

    const FakeFile good = make_bmp(2, 2, k_2x2);
    CHECK(!rejects(good));  // the baseline the cases below break

    FakeFile f = good;
    f.size = sizeof(BitmapHeader) - 1;
    CHECK(rejects(f));  // shorter than the header

    f = good;
    f.header().file_type = 0x1234;
    CHECK(rejects(f));

    f = good;
    f.header().bits_per_pixel = 24;
    CHECK(rejects(f));

    f = good;
    f.header().width = 0;
    CHECK(rejects(f));

    f = good;
    f.header().width = -2;
    CHECK(rejects(f));

    f = good;
    f.header().height = 0;
    CHECK(rejects(f));

    f = good;
    f.header().offset = good.size + 1;
    CHECK(rejects(f));

    f = good;
    f.size -= 1;  // one byte of pixel data missing
    CHECK(rejects(f));

    f = good;
    f.header().width = 1000;
    CHECK(rejects(f));

    f = good;
    f.header().width = 0x7FFFFFFF;
    f.header().height = -0x7FFFFFFF - 1;  // most negative height: largest top-down image
    CHECK(rejects(f));

    f = good;
    f.header().compression = 1;  // RLE8
    CHECK(rejects(f));

    f = make_bmp(2, 2, k_2x2, 3, 0x00FF0000, 0, 0x000000FF);
    CHECK(rejects(f));  // a channel with no bits

    // A missing file has nothing to free.
    LoadedBitmap missing = load_fake(arena, nullptr);
    CHECK(!missing.pixels);
    CHECK_EQ(g_fake_frees, 0);
}
