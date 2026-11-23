// Unit tests. Build and run with test.bat.
//
// The game's .cpp files are included directly, the same unity-build way
// game.cpp does it, so tests can reach everything, static functions included.
// Platform layers aren't tested here: they're mostly OS calls.
#include "arena.h"
#include "asset.cpp"
#include "defer.h"
#include "intrinsics.h"
#include "random.h"
#include "render.cpp"
#include "tile.cpp"
#include "vec.h"

#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

using namespace hh;

// ---- Test harness ----

static const char* g_current_test = "";
static int g_checks = 0;
static int g_failures = 0;

static void report_failure(const char* file, int line, const char* message) {
    std::printf("FAIL %s\n  %s:%d: %s\n", g_current_test, file, line, message);
    g_failures++;
}

#define CHECK(cond)                                    \
    do {                                               \
        g_checks++;                                    \
        if (!(cond)) {                                 \
            report_failure(__FILE__, __LINE__, #cond); \
        }                                              \
    } while (0)

// For integers: prints both values on failure.
#define CHECK_EQ(a, b)                                \
    do {                                              \
        g_checks++;                                   \
        const u64 a_ = static_cast<u64>(a);           \
        const u64 b_ = static_cast<u64>(b);           \
        if (a_ != b_) {                               \
            char msg_[256];                           \
            std::snprintf(                            \
                msg_,                                 \
                sizeof(msg_),                         \
                "%s == %s (0x%llx vs 0x%llx)",        \
                #a,                                   \
                #b,                                   \
                static_cast<unsigned long long>(a_),  \
                static_cast<unsigned long long>(b_)   \
            );                                        \
            report_failure(__FILE__, __LINE__, msg_); \
        }                                             \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                             \
    do {                                                                                  \
        g_checks++;                                                                       \
        const double a_ = static_cast<double>(a);                                         \
        const double b_ = static_cast<double>(b);                                         \
        if (!(std::fabs(a_ - b_) <= static_cast<double>(eps))) {                          \
            char msg_[256];                                                               \
            std::snprintf(msg_, sizeof(msg_), "%s ~= %s (%.9g vs %.9g)", #a, #b, a_, b_); \
            report_failure(__FILE__, __LINE__, msg_);                                     \
        }                                                                                 \
    } while (0)

#define RUN(test)               \
    do {                        \
        g_current_test = #test; \
        test();                 \
    } while (0)

// A failed assert or a bad memory access kills the process before main can
// report anything, so say which test was running. (Not strictly signal-safe,
// but the process is going down anyway.)
static void on_crash(int) {
    std::fprintf(stderr, "CRASH in %s\n", g_current_test);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(1);
}

// Scratch memory for arenas. Static, so tests don't need a 1 MB stack.
alignas(64) static u8 g_arena_memory[1_MB];

// ---- vec.h (compile time) ----

constexpr bool eq(Vec2 a, Vec2 b) {
    return a.x == b.x && a.y == b.y;
}
constexpr bool eq(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
constexpr bool eq(Vec4 a, Vec4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

static_assert(eq(Vec2{1, 2} + Vec2{3, -4}, {4, -2}));
static_assert(eq(Vec2{1, 2} - Vec2{3, -4}, {-2, 6}));
static_assert(eq(-Vec2{1, 2}, {-1, -2}));
static_assert(eq(Vec2{1, 2} * 3.0f, 3.0f * Vec2{1, 2}));
static_assert(dot(Vec2{1, 2}, Vec2{3, 4}) == 11.0f);
static_assert(eq(hadamard(Vec2{2, 3}, Vec2{4, -1}), {8, -3}));
static_assert(eq(hadamard(Vec2{2, 3}, Vec2{5, 1}), hadamard(Vec2{5, 1}, Vec2{2, 3})));
static_assert(length_sq(Vec2{3, 4}) == 25.0f);
static_assert(length_sq(Vec2{-3, 4}) == 25.0f);
static_assert(length_sq(Vec2{}) == 0.0f);
static_assert([] {
    Vec2 v{1, 2};
    v += Vec2{1, 1};
    return eq(v, {2, 3});
}());
static_assert([] {
    Vec2 v{1, 2};
    v *= 3.0f;
    return eq(v, {3, 6});
}());
static_assert([] {
    Vec2 v{1, 2};
    v -= Vec2{4, -1};
    return eq(v, {-3, 3});
}());
static_assert([] {
    Vec2 v{1, 2};
    Vec2& r = (v *= 2.0f);
    Vec2& s = (v -= Vec2{1, 1});
    return &r == &v && &s == &v;
}());

constexpr Vec3 a3{1.0f, 2.0f, 3.0f};
constexpr Vec3 b3{4.0f, -5.0f, 0.5f};
static_assert(eq(a3 + b3, {5.0f, -3.0f, 3.5f}));
static_assert(eq(a3 - b3, {-3.0f, 7.0f, 2.5f}));
static_assert(eq(b3 - a3, -(a3 - b3)));
static_assert(eq(-a3, {-1.0f, -2.0f, -3.0f}));
static_assert(eq(-(-a3), a3));
static_assert(eq(a3 * 2.0f, {2.0f, 4.0f, 6.0f}));
static_assert(eq(2.0f * a3, a3 * 2.0f));
static_assert(eq(a3 * -1.0f, -a3));
static_assert(dot(a3, b3) == 4.0f - 10.0f + 1.5f);
static_assert(dot(a3, b3) == dot(b3, a3));
static_assert(dot(Vec3{0, 0, 1}, Vec3{0, 0, 1}) == 1.0f);  // z participates
static_assert(dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == 0.0f);
static_assert(eq(hadamard(a3, b3), {4.0f, -10.0f, 1.5f}));
static_assert(eq(hadamard(a3, Vec3{1, 1, 1}), a3));
static_assert(length_sq(Vec3{2, 3, 6}) == 49.0f);
static_assert(length_sq(Vec3{0, 0, -2}) == 4.0f);  // z participates
static_assert([] {
    Vec3 v = a3;
    v += b3;
    return eq(v, a3 + b3);
}());
static_assert([] {
    Vec3 v = a3;
    Vec3& r = (v += b3);
    return &r == &v;
}());
static_assert([] {
    Vec3 v = a3;
    v *= -2.0f;
    return eq(v, a3 * -2.0f);
}());
static_assert([] {
    Vec3 v = a3;
    v -= b3;
    return eq(v, a3 - b3);
}());
static_assert([] {
    Vec3 v = a3;
    v -= Vec3{0, 0, 3};  // z participates
    return eq(v, {1, 2, 0});
}());
static_assert([] {
    Vec3 v = a3;
    Vec3& r = (v *= 2.0f);
    Vec3& s = (v -= b3);
    return &r == &v && &s == &v;
}());

constexpr Vec4 a4{1.0f, 2.0f, 3.0f, 4.0f};
constexpr Vec4 b4{-2.0f, 0.25f, 8.0f, -1.0f};
static_assert(eq(a4 + b4, {-1.0f, 2.25f, 11.0f, 3.0f}));
static_assert(eq(a4 - b4, {3.0f, 1.75f, -5.0f, 5.0f}));
static_assert(eq(b4 - a4, -(a4 - b4)));
static_assert(eq(-a4, {-1.0f, -2.0f, -3.0f, -4.0f}));
static_assert(eq(a4 * 0.5f, {0.5f, 1.0f, 1.5f, 2.0f}));
static_assert(eq(0.5f * a4, a4 * 0.5f));
static_assert(dot(a4, b4) == -2.0f + 0.5f + 24.0f - 4.0f);
static_assert(dot(a4, b4) == dot(b4, a4));
static_assert(dot(Vec4{0, 0, 0, 1}, Vec4{0, 0, 0, 1}) == 1.0f);  // w participates
static_assert(eq(hadamard(a4, b4), {-2.0f, 0.5f, 24.0f, -4.0f}));
static_assert(eq(hadamard(a4, Vec4{1, 1, 1, 1}), a4));
static_assert(length_sq(Vec4{1, 1, 1, 1}) == 4.0f);
static_assert(length_sq(Vec4{0, 0, 0, -3}) == 9.0f);  // w participates
static_assert([] {
    Vec4 v = a4;
    v += b4;
    return eq(v, a4 + b4);
}());
static_assert([] {
    Vec4 v = a4;
    Vec4& r = (v += b4);
    return &r == &v;
}());
static_assert([] {
    Vec4 v = a4;
    v *= 0.5f;
    return eq(v, a4 * 0.5f);
}());
static_assert([] {
    Vec4 v = a4;
    v -= b4;
    return eq(v, a4 - b4);
}());
static_assert([] {
    Vec4 v = a4;
    v -= Vec4{0, 0, 0, 4};  // w participates
    return eq(v, {1, 2, 3, 0});
}());
static_assert([] {
    Vec4 v = a4;
    Vec4& r = (v *= 2.0f);
    Vec4& s = (v -= b4);
    return &r == &v && &s == &v;
}());

static_assert(sizeof(Vec3) == 12 && sizeof(Vec4) == 16);

// length uses sqrtf, which isn't constexpr, so it's checked at runtime.
static void test_vec_length() {
    CHECK_NEAR(length(Vec2{3, 4}), 5.0f, 1e-6f);
    CHECK_NEAR(length(Vec2{-3, 4}), 5.0f, 1e-6f);
    CHECK_NEAR(length(Vec2{1, 1}), 1.41421356f, 1e-6f);  // a keyboard diagonal
    CHECK_EQ(length(Vec2{}), 0.0f);
    CHECK_NEAR(length(Vec2{0, -2}), 2.0f, 0.0f);  // one component zero: exact

    CHECK_NEAR(length(Vec3{2, 3, 6}), 7.0f, 1e-6f);
    CHECK_NEAR(length(Vec3{0, 0, -2}), 2.0f, 0.0f);
    CHECK_EQ(length(Vec3{}), 0.0f);

    CHECK_NEAR(length(Vec4{1, 1, 1, 1}), 2.0f, 1e-6f);
    CHECK_NEAR(length(Vec4{0, 0, 0, -3}), 3.0f, 0.0f);
    CHECK_EQ(length(Vec4{}), 0.0f);

    // Scaling scales the length; the length is never negative.
    const Vec2 v = {-1.5f, 2.0f};
    CHECK_NEAR(length(-3.0f * v), 3.0f * length(v), 1e-5f);
    CHECK_NEAR(length(v) * length(v), length_sq(v), 1e-5f);
}

// ---- Arena ----

static void test_arena_aligns_pushes() {
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

static void test_arena_fills_exactly_and_resets() {
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

static void test_random_walks_and_wraps() {
    const u32 table_size = static_cast<u32>(std::size(k_random_number_table));

    RandomSeries series = {.index = 0};
    CHECK_EQ(random_next(series), k_random_number_table[0]);
    CHECK_EQ(series.index, 1);

    series.index = table_size - 1;
    CHECK_EQ(random_next(series), k_random_number_table[table_size - 1]);
    CHECK_EQ(series.index, 0);
}

static void test_random_is_deterministic_and_in_range() {
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

static void test_bitscan_forward() {
    CHECK(!bitscan_forward(0).found);

    BitscanResult one = bitscan_forward(1);
    CHECK(one.found);
    CHECK_EQ(one.index, 0);

    CHECK_EQ(bitscan_forward(0b1100).index, 2);
    CHECK_EQ(bitscan_forward(0x80000000).index, 31);
    CHECK_EQ(bitscan_forward(0xFFFFFFFF).index, 0);
}

static void test_defer_runs_in_reverse_at_scope_exit() {
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

// ---- tile.cpp ----

// World is too big for the stack, so it lives on the heap. Zeroed is empty.
static World* make_world() {
    World* world = static_cast<World*>(std::calloc(1, sizeof(World)));
    world->tile_side_in_meters = k_tile_side_in_meters;
    world->tile_side_in_pixels = k_tile_side_in_pixels;
    world->meters_to_pixels = k_tile_side_in_pixels / k_tile_side_in_meters;
    return world;
}

static Arena make_test_arena() {
    return Arena::make(g_arena_memory, sizeof(g_arena_memory));
}

static bool is_canonical(const World& world, const WorldPosition& pos) {
    const f32 side = world.tile_side_in_meters;
    return pos.tile_rel.x >= 0.0f && pos.tile_rel.x < side && pos.tile_rel.y >= 0.0f && pos.tile_rel.y < side;
}

static void test_chunk_position_splits_bits() {
    ChunkPosition last_in_chunk = get_chunk_position(15, 15, 3);
    CHECK_EQ(last_in_chunk.chunk_x, 0);
    CHECK_EQ(last_in_chunk.tile_x, 15);
    CHECK_EQ(last_in_chunk.chunk_y, 0);
    CHECK_EQ(last_in_chunk.tile_y, 15);
    CHECK_EQ(last_in_chunk.chunk_z, 3);  // z isn't chunked

    ChunkPosition first_in_next = get_chunk_position(16, 33, 0);
    CHECK_EQ(first_in_next.chunk_x, 1);
    CHECK_EQ(first_in_next.tile_x, 0);
    CHECK_EQ(first_in_next.chunk_y, 2);
    CHECK_EQ(first_in_next.tile_y, 1);

    ChunkPosition top = get_chunk_position(0xFFFFFFFF, 0, 0);
    CHECK_EQ(top.chunk_x, 0x0FFFFFFF);
    CHECK_EQ(top.tile_x, 15);
}

static void test_world_pos_offset_carries_into_neighbor_tiles() {
    World* world = make_world();
    defer(std::free(world));
    const f32 side = world->tile_side_in_meters;
    const WorldPosition start = {.abs_tile_x = 5, .abs_tile_y = 5, .abs_tile_z = 0, .tile_rel = {0.7f, 0.7f}};

    WorldPosition right = world_pos_offset(*world, start, {1.0f, 0.0f});
    CHECK_EQ(right.abs_tile_x, 6);
    CHECK_NEAR(right.tile_rel.x, 0.3f, 1e-5f);
    CHECK_EQ(right.abs_tile_y, 5);  // the other axis is untouched
    CHECK_EQ(right.tile_rel.y, start.tile_rel.y);

    WorldPosition left = world_pos_offset(*world, start, {-1.0f, 0.0f});
    CHECK_EQ(left.abs_tile_x, 4);
    CHECK_NEAR(left.tile_rel.x, 1.1f, 1e-5f);

    WorldPosition far_up = world_pos_offset(*world, start, {0.0f, 5.0f * side + 0.2f});
    CHECK_EQ(far_up.abs_tile_y, 10);
    CHECK_NEAR(far_up.tile_rel.y, 0.9f, 1e-5f);

    WorldPosition far_down = world_pos_offset(*world, start, {0.0f, -3.0f * side});
    CHECK_EQ(far_down.abs_tile_y, 2);
    CHECK_NEAR(far_down.tile_rel.y, 0.7f, 1e-5f);

    // Landing exactly on the boundary belongs to the next tile.
    WorldPosition on_edge = world_pos_offset(*world, {.abs_tile_x = 5}, {side, 0.0f});
    CHECK_EQ(on_edge.abs_tile_x, 6);
    CHECK_EQ(on_edge.tile_rel.x, 0.0f);
}

static void test_world_pos_offset_tiny_negative_stays_canonical() {
    World* world = make_world();
    defer(std::free(world));

    // -1e-8 + 1.4 rounds to exactly 1.4f: the case recanonicalize_coord
    // corrects after the floor.
    WorldPosition pos = world_pos_offset(*world, {.abs_tile_x = 5}, {-1e-8f, 0.0f});
    CHECK(is_canonical(*world, pos));
    CHECK_EQ(pos.abs_tile_x, 5);
    CHECK_EQ(pos.tile_rel.x, 0.0f);
}

static void test_world_pos_offset_left_of_zero_wraps_off_world() {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();
    set_tile_value(arena, *world, 0, 0, 0, k_tile_empty);

    WorldPosition pos = world_pos_offset(*world, {.tile_rel = {0.5f, 0.5f}}, {-1.0f, 0.0f});
    CHECK_EQ(pos.abs_tile_x, 0xFFFFFFFF);
    CHECK_NEAR(pos.tile_rel.x, 0.9f, 1e-5f);
    CHECK(!is_world_pos_walkable(*world, pos));
}

static void test_random_walk_stays_canonical_and_tracks_distance() {
    World* world = make_world();
    defer(std::free(world));

    // Starts at tile 0, so the walk crosses the u32 wrap in both directions.
    const WorldPosition start = {.tile_rel = {0.1f, 1.3f}};
    WorldPosition pos = start;
    double total_dx = 0.0;
    double total_dy = 0.0;
    bool canonical = true;
    RandomSeries series = {.index = 3};
    for (int i = 0; i < 10000; i++) {
        // Steps up to 10 m either way, several tiles at once.
        const f32 dx = static_cast<f32>(static_cast<i32>(random_choice(series, 2001)) - 1000) / 100.0f;
        const f32 dy = static_cast<f32>(static_cast<i32>(random_choice(series, 2001)) - 1000) / 100.0f;
        pos = world_pos_offset(*world, pos, {dx, dy});
        total_dx += static_cast<double>(dx);
        total_dy += static_cast<double>(dy);
        canonical = canonical && is_canonical(*world, pos);
    }
    CHECK(canonical);

    Vec2 diff = world_pos_diff(*world, pos, start);
    CHECK_NEAR(diff.x, total_dx, 0.01);
    CHECK_NEAR(diff.y, total_dy, 0.01);
}

static void test_world_pos_diff_is_signed() {
    World* world = make_world();
    defer(std::free(world));
    const f32 side = world->tile_side_in_meters;

    const WorldPosition a = {.abs_tile_x = 7, .abs_tile_y = 3, .tile_rel = {0.2f, 0.5f}};
    const WorldPosition b = {.abs_tile_x = 5, .abs_tile_y = 4, .tile_rel = {1.0f, 0.5f}};

    Vec2 ab = world_pos_diff(*world, a, b);
    CHECK_NEAR(ab.x, 2.0f * side - 0.8f, 1e-5f);
    CHECK_NEAR(ab.y, -side, 1e-5f);

    Vec2 ba = world_pos_diff(*world, b, a);
    CHECK_NEAR(ba.x, -ab.x, 1e-5f);
    CHECK_NEAR(ba.y, -ab.y, 1e-5f);

    // Across the wrap: tile 0xFFFFFFFF is one tile left of tile 0.
    const WorldPosition wrapped = {.abs_tile_x = 0xFFFFFFFF, .tile_rel = {0.5f, 0.0f}};
    const WorldPosition one = {.abs_tile_x = 1, .tile_rel = {0.5f, 0.0f}};
    CHECK_NEAR(world_pos_diff(*world, wrapped, one).x, -2.0f * side, 1e-5f);
    CHECK_NEAR(world_pos_diff(*world, one, wrapped).x, 2.0f * side, 1e-5f);

    // z is ignored.
    WorldPosition other_level = a;
    other_level.abs_tile_z = 9;
    CHECK_NEAR(world_pos_diff(*world, other_level, a).x, 0.0f, 0.0f);
}

static void test_tile_chunk_lookup_does_not_allocate() {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();

    CHECK(get_tile_chunk(*world, 1, 2, 3) == nullptr);
    CHECK_EQ(arena.used, 0);

    TileChunk* created = get_tile_chunk(*world, 1, 2, 3, &arena);
    CHECK(created != nullptr);
    const u64 used = arena.used;

    // Found again with or without an arena, without allocating more.
    CHECK(get_tile_chunk(*world, 1, 2, 3) == created);
    CHECK(get_tile_chunk(*world, 1, 2, 3, &arena) == created);
    CHECK_EQ(arena.used, used);

    bool all_uninitialized = true;
    for (u32 i = 0; i < k_chunk_dim * k_chunk_dim; i++) {
        all_uninitialized = all_uninitialized && created->tiles[i] == k_tile_uninitialized;
    }
    CHECK(all_uninitialized);
}

static void test_tile_chunk_hash_collisions_chain() {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();

    // hash = x * 19 + y * 7 + z * 3: all three of these hash to 133.
    TileChunk* a = get_tile_chunk(*world, 7, 0, 0, &arena);
    TileChunk* b = get_tile_chunk(*world, 0, 19, 0, &arena);
    CHECK(a != nullptr && b != nullptr && a != b);
    CHECK(&world->chunk_hash[133] == a);
    CHECK(world->chunk_hash[133].next == b);

    CHECK(get_tile_chunk(*world, 7, 0, 0) == a);
    CHECK(get_tile_chunk(*world, 0, 19, 0) == b);

    // Same slot, not in the chain.
    const u64 used = arena.used;
    CHECK(get_tile_chunk(*world, 0, 1, 42) == nullptr);
    CHECK_EQ(arena.used, used);

    // Collide only after masking, and differ in just one coordinate, so
    // matching has to compare each of x, y and z.
    TileChunk* origin = get_tile_chunk(*world, 0, 0, 0, &arena);
    TileChunk* far_x = get_tile_chunk(*world, 4096, 0, 0, &arena);
    TileChunk* far_y = get_tile_chunk(*world, 0, 4096, 0, &arena);
    TileChunk* far_z = get_tile_chunk(*world, 0, 0, 4096, &arena);
    CHECK(origin != far_x && origin != far_y && origin != far_z);
    CHECK(far_x != far_y && far_y != far_z && far_x != far_z);
    CHECK(get_tile_chunk(*world, 0, 0, 0) == origin);
    CHECK(get_tile_chunk(*world, 4096, 0, 0) == far_x);
    CHECK(get_tile_chunk(*world, 0, 4096, 0) == far_y);
    CHECK(get_tile_chunk(*world, 0, 0, 4096) == far_z);
}

static void test_tile_values_and_walkability() {
    World* world = make_world();
    defer(std::free(world));
    Arena arena = make_test_arena();

    // No chunk: false, and value is left alone.
    u32 value = 12345;
    CHECK(!get_tile_value(*world, 20, 3, 0, value));
    CHECK_EQ(value, 12345);
    CHECK(!is_tile_walkable(*world, 20, 3, 0));

    set_tile_value(arena, *world, 20, 3, 0, k_tile_wall);
    CHECK(get_tile_value(*world, 20, 3, 0, value));
    CHECK_EQ(value, k_tile_wall);
    CHECK(!is_tile_walkable(*world, 20, 3, 0));

    // Same chunk, never written.
    CHECK(get_tile_value(*world, 21, 3, 0, value));
    CHECK_EQ(value, k_tile_uninitialized);
    CHECK(!is_tile_walkable(*world, 21, 3, 0));

    // Same x and y on another level is a different chunk.
    CHECK(!get_tile_value(*world, 20, 3, 1, value));

    set_tile_value(arena, *world, 22, 3, 0, k_tile_empty);
    set_tile_value(arena, *world, 23, 3, 0, k_tile_stairs_up);
    set_tile_value(arena, *world, 24, 3, 0, k_tile_stairs_down);
    CHECK(is_tile_walkable(*world, 22, 3, 0));
    CHECK(is_tile_walkable(*world, 23, 3, 0));
    CHECK(is_tile_walkable(*world, 24, 3, 0));

    // Overwriting.
    set_tile_value(arena, *world, 20, 3, 0, k_tile_empty);
    CHECK(is_tile_walkable(*world, 20, 3, 0));

    // The WorldPosition overload reads the same tile.
    const WorldPosition pos = {.abs_tile_x = 23, .abs_tile_y = 3, .abs_tile_z = 0, .tile_rel = {0.5f, 0.0f}};
    CHECK(get_tile_value(*world, pos, value));
    CHECK_EQ(value, k_tile_stairs_up);
    CHECK(is_world_pos_walkable(*world, pos));
}

// ---- render.cpp ----

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

static void test_screen_buffer_size() {
    // Width and height, not pitch: the test screen's rows are 10 pixels apart.
    TestScreen screen;
    const Vec2 size = screen.buffer.size_vec();
    CHECK_EQ(size.x, 8.0f);
    CHECK_EQ(size.y, 8.0f);
}

static void test_render_rect_clips() {
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

static void test_render_rect_rounds_edges() {
    TestScreen screen;
    // x: round(0.4) = 0 to round(2.6) = 3. y: round(1.5) = 2 to round(3.49) = 3.
    render_rect(screen.buffer, {0.4f, 1.5f}, {2.6f, 3.49f}, {0.0f, 1.0f, 0.0f});
    CHECK_EQ(screen.written(), 3);
    CHECK_EQ(screen.at(0, 2), pack_color(0, 255, 0));
    CHECK_EQ(screen.at(2, 2), pack_color(0, 255, 0));
    CHECK_EQ(screen.at(3, 2), TestScreen::k_sentinel);
}

static void test_draw_bitmap_clips() {
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

static void test_draw_bitmap_blends_alpha() {
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

static void test_draw_bitmap_fullscreen_scales() {
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

// ---- asset.cpp ----

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

static void test_load_bmp_row_order() {
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

static void test_load_bmp_bitfield_masks() {
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

static void test_load_bmp_rejects_bad_files() {
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

// ---- main ----

int main() {
    std::signal(SIGABRT, on_crash);
    std::signal(SIGSEGV, on_crash);

    RUN(test_vec_length);

    RUN(test_arena_aligns_pushes);
    RUN(test_arena_fills_exactly_and_resets);

    RUN(test_random_walks_and_wraps);
    RUN(test_random_is_deterministic_and_in_range);

    RUN(test_bitscan_forward);
    RUN(test_defer_runs_in_reverse_at_scope_exit);

    RUN(test_chunk_position_splits_bits);
    RUN(test_world_pos_offset_carries_into_neighbor_tiles);
    RUN(test_world_pos_offset_tiny_negative_stays_canonical);
    RUN(test_world_pos_offset_left_of_zero_wraps_off_world);
    RUN(test_random_walk_stays_canonical_and_tracks_distance);
    RUN(test_world_pos_diff_is_signed);
    RUN(test_tile_chunk_lookup_does_not_allocate);
    RUN(test_tile_chunk_hash_collisions_chain);
    RUN(test_tile_values_and_walkability);

    RUN(test_screen_buffer_size);
    RUN(test_render_rect_clips);
    RUN(test_render_rect_rounds_edges);
    RUN(test_draw_bitmap_clips);
    RUN(test_draw_bitmap_blends_alpha);
    RUN(test_draw_bitmap_fullscreen_scales);

    RUN(test_load_bmp_row_order);
    RUN(test_load_bmp_bitfield_masks);
    RUN(test_load_bmp_rejects_bad_files);

    if (g_failures) {
        std::printf("%d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    std::printf("all %d checks passed\n", g_checks);
    return 0;
}
