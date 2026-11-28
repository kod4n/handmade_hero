// Tests for src/arena.h, src/random.h, src/intrinsics.h and src/defer.h.
// Built as part of tests/main.cpp; the includes are here so clangd can make
// sense of this file on its own.

#include "test.h"
#include "test_support.h"
#include "arena.h"
#include "defer.h"
#include "intrinsics.h"
#include "random.h"

using namespace hh;

// ---- intrinsics.h (compile time) ----

static_assert(sign(5) == 1);
static_assert(sign(-5) == -1);
static_assert(sign(0) == 0);
static_assert(sign(INT32_MAX) == 1);
static_assert(sign(INT32_MIN) == -1);  // no negation, so no overflow

// Qualified: with `using namespace hh`, a bare abs/atan2 is ambiguous with
// the global ones from <cmath>/<stdlib.h>.
static_assert(hh::abs(5) == 5);
static_assert(hh::abs(-5) == 5);
static_assert(hh::abs(0) == 0);
static_assert(hh::abs(INT32_MAX) == INT32_MAX);
static_assert(hh::abs(-INT32_MAX) == INT32_MAX);
static_assert(hh::abs(INT32_MIN) == INT32_MIN);  // no positive i32 to return; wraps, no UB

// ---- Arena ----

TEST(arena_aligns_pushes) {
    Arena arena = Arena::make(g_arena_memory, 256);

    u8* byte = arena.push<u8>();
    CHECK(byte == g_arena_memory);
    CHECK_EQ(arena.used, 1);

    u64* wide = arena.push<u64>();
    CHECK(reinterpret_cast<u8*>(wide) == g_arena_memory + 8);
    CHECK_EQ(arena.used, 16);

    std::span<u32> array = arena.push_array<u32>(3);
    CHECK_EQ(array.size(), 3);
    CHECK(reinterpret_cast<u8*>(array.data()) == g_arena_memory + 16);
    CHECK_EQ(arena.used, 28);

    // Already aligned: no padding.
    u32* aligned = arena.push<u32>();
    CHECK(reinterpret_cast<u8*>(aligned) == g_arena_memory + 28);
    CHECK_EQ(arena.used, 32);
}

TEST(arena_fills_exactly_and_resets) {
    Arena arena = Arena::make(g_arena_memory, 64);

    std::span<u8> all = arena.push_array<u8>(64);
    CHECK_EQ(all.size(), 64);
    CHECK_EQ(arena.used, arena.size);

    std::span<u32> none = arena.push_array<u32>(0);
    CHECK_EQ(none.size(), 0);

    arena.used = 0;
    CHECK(arena.push<u8>() == g_arena_memory);
}

// ---- random.h ----

TEST(random_walks_and_wraps) {
    const u32 table_size = static_cast<u32>(std::size(k_random_number_table));

    RandomSeries series = {.index = 0};
    CHECK_EQ(random_next(series), k_random_number_table[0]);
    CHECK_EQ(series.index, 1);

    series.index = table_size - 1;
    CHECK_EQ(random_next(series), k_random_number_table[table_size - 1]);
    CHECK_EQ(series.index, 0);
}

TEST(random_is_deterministic_and_in_range) {
    RandomSeries a = {.index = 17};
    RandomSeries b = {.index = 17};
    bool same = true;
    bool in_range = true;
    for (int i = 0; i < 2000; i++) {
        same = same && random_next(a) == random_next(b);
        in_range = in_range && random_choice(a, 7) < 7 && random_choice(b, 1) == 0;
    }
    CHECK(same);
    CHECK(in_range);
}

// ---- intrinsics.h / defer.h ----

TEST(abs_f32) {
    CHECK_EQ(hh::abs(1.5f), 1.5f);
    CHECK_EQ(hh::abs(-1.5f), 1.5f);
    CHECK_EQ(hh::abs(0.0f), 0.0f);
    CHECK(!std::signbit(hh::abs(-0.0f)));  // -0 becomes +0
    CHECK_EQ(hh::abs(-INFINITY), INFINITY);
}

// Arguments in the standard order, y first: hh::atan2(1, 0) is straight up.
TEST(atan2_takes_y_then_x) {
    constexpr f32 pi = 3.14159265f;
    CHECK_NEAR(hh::atan2(1.0f, 0.0f), 0.5f * pi, 1e-6f);
    CHECK_NEAR(hh::atan2(0.0f, 1.0f), 0.0f, 1e-6f);
    CHECK_NEAR(hh::atan2(0.0f, -1.0f), pi, 1e-6f);
    CHECK_NEAR(hh::atan2(-1.0f, 0.0f), -0.5f * pi, 1e-6f);
    CHECK_NEAR(hh::atan2(1.0f, 1.0f), 0.25f * pi, 1e-6f);
}

// rotate_left/right use _rotl/_rotr on MSVC-compatible compilers and
// std::rotl/rotr elsewhere; both must agree, including negative amounts.
TEST(rotate) {
    CHECK_EQ(rotate_left(0x80000001u, 1), 0x00000003u);
    CHECK_EQ(rotate_right(0x80000001u, 1), 0xC0000000u);
    CHECK_EQ(rotate_left(0x12345678u, 0), 0x12345678u);
    CHECK_EQ(rotate_left(0x12345678u, 32), 0x12345678u);
    CHECK_EQ(rotate_left(0x12345678u, 8), 0x34567812u);
    CHECK_EQ(rotate_left(0x12345678u, -8), rotate_right(0x12345678u, 8));
    CHECK_EQ(rotate_right(0x12345678u, -8), rotate_left(0x12345678u, 8));
}

TEST(bitscan_forward) {
    CHECK(!bitscan_forward(0).found);

    BitscanResult one = bitscan_forward(1);
    CHECK(one.found);
    CHECK_EQ(one.index, 0);

    CHECK_EQ(bitscan_forward(0b1100).index, 2);
    CHECK_EQ(bitscan_forward(0x80000000).index, 31);
    CHECK_EQ(bitscan_forward(0xFFFFFFFF).index, 0);
}

TEST(defer_runs_in_reverse_at_scope_exit) {
    int order[2] = {};
    int count = 0;
    {
        defer(order[count++] = 1);
        defer(order[count++] = 2);
        CHECK_EQ(count, 0);
    }
    CHECK_EQ(count, 2);
    CHECK_EQ(order[0], 2);
    CHECK_EQ(order[1], 1);
}
