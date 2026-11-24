#pragma once

// Helpers shared by more than one test file.

#include "arena.h"
#include "types.h"
#include "vec.h"

// Scratch memory for arenas. Static, so tests don't need a 1 MB stack.
alignas(64) inline u8 g_arena_memory[1_MB];

inline hh::Arena make_test_arena() {
    return hh::Arena::make(g_arena_memory, sizeof(g_arena_memory));
}

// How vectors print and compare in checks. They live in hh, next to the
// types, so the checks in test.h find them by argument-dependent lookup.
namespace hh {

inline void test_print(Vec2 v) {
    std::printf("{%.9g, %.9g}", static_cast<double>(v.x), static_cast<double>(v.y));
}
inline void test_print(Vec3 v) {
    std::printf("{%.9g, %.9g, %.9g}", static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
}
inline void test_print(Vec4 v) {
    std::printf(
        "{%.9g, %.9g, %.9g, %.9g}",
        static_cast<double>(v.x),
        static_cast<double>(v.y),
        static_cast<double>(v.z),
        static_cast<double>(v.w)
    );
}

inline bool test_equal(Vec2 a, Vec2 b) {
    return a.x == b.x && a.y == b.y;
}
inline bool test_equal(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline bool test_equal(Vec4 a, Vec4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

// Each component within tolerance.
inline bool test_near(Vec2 a, Vec2 b, f32 tolerance) {
    return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance;
}
inline bool test_near(Vec3 a, Vec3 b, f32 tolerance) {
    return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance && std::fabs(a.z - b.z) <= tolerance;
}
inline bool test_near(Vec4 a, Vec4 b, f32 tolerance) {
    return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance && std::fabs(a.z - b.z) <= tolerance
           && std::fabs(a.w - b.w) <= tolerance;
}

}  // namespace hh
