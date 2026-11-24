// Tests for src/vec.h. Built as part of tests/main.cpp; the includes are here
// so clangd can make sense of this file on its own.

#include "test.h"
#include "vec.h"

using namespace hh;

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
TEST(vec_length) {
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
