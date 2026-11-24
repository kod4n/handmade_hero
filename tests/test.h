#pragma once

// Minimal test framework. Test files (tests/test_*.cpp) are #included by
// tests/main.cpp as one unity build, so tests always run in source order.
//
//   TEST(vec2_add) {
//       Vec2 a = {1, 2};
//       CHECK_EQ(a + a, (Vec2{2, 4}));  // braces inside a macro argument need parentheses
//   }
//
// Checks:
//   CHECK(cond)
//   CHECK_EQ(actual, expected)
//   CHECK_NEAR(actual, expected, tolerance)
// A failed check is reported and the test keeps going, so one run shows every
// broken check. Each returns false on failure: `if (!CHECK(p)) return;` stops a
// test whose later checks depend on this one (or would repeat it in a loop).
//
// Numbers, bools and strings print automatically in failure messages. For your
// own types, define these next to their tests, in the type's namespace (hh for
// game types), so argument-dependent lookup finds them:
//   void test_print(T v)                 how values print in failure messages
//   bool test_equal(T a, T b)            for CHECK_EQ, if T has no operator==
//   bool test_near(T a, T b, f32 tol)    for CHECK_NEAR
//
// Random inputs: test_rand_*() are seeded from the test's name, so every run of
// a test sees the same values, and adding a test doesn't change the values
// other tests get. `--seed N` mixes N in, to try different values.

#include "types.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

using TestFn = void();

struct Test {
    const char* name;
    const char* file;
    i32 line;
    TestFn* fn;
};

inline constexpr i32 k_max_tests = 1024;

struct TestState {
    // Filled in by TEST() before main runs.
    Test tests[k_max_tests];
    i32 test_count;
    bool too_many_tests;

    u64 seed;  // from --seed

    // The running test.
    const Test* current;
    i32 checks;
    i32 failed_checks;
    u64 rng_state;
    bool rng_used;
};

// Zero-initialized before any constructor runs, so TEST() registration can't
// run before it's ready.
inline TestState g_test_state;

struct TestRegistrar {
    TestRegistrar(const char* name, const char* file, i32 line, TestFn* fn) {
        TestState& state = g_test_state;
        if (state.test_count == k_max_tests) {
            state.too_many_tests = true;
            return;
        }
        state.tests[state.test_count++] = {name, file, line, fn};
    }
};

#define TEST(name)                                                                      \
    static void test_##name();                                                          \
    static TestRegistrar test_registrar_##name(#name, __FILE__, __LINE__, test_##name); \
    static void test_##name()

#define CHECK(cond) test_check((cond), "CHECK(" #cond ")", __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) \
    test_check_eq((actual), (expected), "CHECK_EQ(" #actual ", " #expected ")", #actual, #expected, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance)                   \
    test_check_near(                                              \
        (actual),                                                 \
        (expected),                                               \
        (tolerance),                                              \
        "CHECK_NEAR(" #actual ", " #expected ", " #tolerance ")", \
        #actual,                                                  \
        #expected,                                                \
        __FILE__,                                                 \
        __LINE__                                                  \
    )

// ---- Failure output ----

// Prints the test's FAIL line before its first failed check, then the check as
// file:line: error:, which neovim's quickfix (default 'errorformat') jumps to.
inline void test_report_failure(const char* file, i32 line, const char* check) {
    TestState& state = g_test_state;
    if (state.failed_checks == 0) {
        std::printf("FAIL %s\n", state.current->name);
    }
    ++state.failed_checks;
    std::printf("%s:%d: error: %s failed\n", file, line, check);
}

// Built-in types print here; anything else goes to a test_print found by ADL.
template <typename T>
void test_print_value(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        std::fputs(value ? "true" : "false", stdout);
    } else if constexpr (std::is_same_v<T, char>) {
        std::printf("'%c'", value);
    } else if constexpr (std::is_same_v<T, f32>) {
        // 9 significant digits: enough to tell any two f32s apart.
        std::printf("%.9g", static_cast<double>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
        std::printf("%.17g", static_cast<double>(value));  // same for double
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        std::printf("%lld", static_cast<long long>(value));
    } else if constexpr (std::is_integral_v<T>) {
        // Hex too, since unsigned values here are often packed colors or bits.
        const auto u = static_cast<unsigned long long>(value);
        std::printf("%llu (0x%llX)", u, u);
    } else if constexpr (std::is_enum_v<T>) {
        std::printf("%lld", static_cast<long long>(value));
    } else if constexpr (std::is_convertible_v<const T&, const char*>) {
        const char* string = value;
        if (string) {
            std::printf("\"%s\"", string);
        } else {
            std::fputs("nullptr", stdout);
        }
    } else if constexpr (std::is_pointer_v<T>) {
        std::printf("%p", static_cast<const void*>(value));
    } else {
        // A compile error here means T needs a test_print(T); see the top of this file.
        test_print(value);
    }
}

// One "    label = value" line, labels padded to width so the values line up.
template <typename T>
void test_print_labeled(const char* label, i32 width, const T& value) {
    std::printf("    %-*s = ", width, label);
    test_print_value(value);
    std::putchar('\n');
}

inline i32 test_label_width(const char* a, const char* b) {
    const size_t width = std::strlen(a) > std::strlen(b) ? std::strlen(a) : std::strlen(b);
    return static_cast<i32>(width);
}

// ---- Checks ----

inline bool test_check(bool ok, const char* check, const char* file, i32 line) {
    ++g_test_state.checks;
    if (!ok) {
        test_report_failure(file, line, check);
    }
    return ok;
}

// Integer types other than bool and characters, which std::cmp_equal rejects.
template <typename T>
constexpr bool test_is_integer =
    std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t>
    && !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>;

template <typename A, typename B>
bool test_values_equal(const A& a, const B& b) {
    if constexpr (test_is_integer<A> && test_is_integer<B>) {
        // So CHECK_EQ(some_u32, 3) compares by value, not after a
        // signed/unsigned conversion.
        return std::cmp_equal(a, b);
    } else if constexpr (std::is_convertible_v<const A&, const char*> && std::is_convertible_v<const B&, const char*>) {
        // Strings compare by content, not by pointer.
        const char* a_string = a;
        const char* b_string = b;
        if (!a_string || !b_string) {
            return a_string == b_string;
        }
        return std::strcmp(a_string, b_string) == 0;
    } else if constexpr (requires { a == b; }) {
        return a == b;
    } else {
        // A compile error here means the type needs operator== or test_equal; see the top of this file.
        return test_equal(a, b);
    }
}

template <typename A, typename B>
bool test_check_eq(
    const A& actual,
    const B& expected,
    const char* check,
    const char* actual_text,
    const char* expected_text,
    const char* file,
    i32 line
) {
    ++g_test_state.checks;
    const bool ok = test_values_equal(actual, expected);
    if (!ok) {
        test_report_failure(file, line, check);
        const i32 width = test_label_width(actual_text, expected_text);
        test_print_labeled(actual_text, width, actual);
        test_print_labeled(expected_text, width, expected);
    }
    return ok;
}

template <typename A, typename B, typename Tolerance>
bool test_check_near(
    const A& actual,
    const B& expected,
    const Tolerance& tolerance,
    const char* check,
    const char* actual_text,
    const char* expected_text,
    const char* file,
    i32 line
) {
    ++g_test_state.checks;
    constexpr bool numbers = std::is_arithmetic_v<A> && std::is_arithmetic_v<B>;
    bool ok;
    if constexpr (numbers) {
        // Written so that NaN fails: every comparison with NaN is false.
        const double difference = std::fabs(static_cast<double>(actual) - static_cast<double>(expected));
        ok = difference <= static_cast<double>(tolerance);
    } else {
        // A compile error here means the type needs test_near; see the top of this file.
        ok = test_near(actual, expected, tolerance);
    }
    if (!ok) {
        test_report_failure(file, line, check);
        i32 width = test_label_width(actual_text, expected_text);
        if (width < static_cast<i32>(std::strlen("difference"))) {
            width = static_cast<i32>(std::strlen("difference"));
        }
        test_print_labeled(actual_text, width, actual);
        test_print_labeled(expected_text, width, expected);
        if constexpr (numbers) {
            const double difference = std::fabs(static_cast<double>(actual) - static_cast<double>(expected));
            test_print_labeled("difference", width, difference);
        }
        test_print_labeled("tolerance", width, tolerance);
    }
    return ok;
}

// ---- Random inputs ----

inline u64 test_hash(const char* text) {
    u64 hash = 0xCBF29CE484222325ull;  // FNV-1a
    for (; *text; ++text) {
        hash ^= static_cast<u8>(*text);
        hash *= 0x100000001B3ull;
    }
    return hash;
}

inline u64 test_seed_for(const Test& test, u64 seed) {
    return test_hash(test.name) ^ (seed * 0x9E3779B97F4A7C15ull);
}

inline u64 test_rand_u64() {
    TestState& state = g_test_state;
    state.rng_used = true;
    u64 z = (state.rng_state += 0x9E3779B97F4A7C15ull);  // splitmix64
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline u32 test_rand_u32() {
    return static_cast<u32>(test_rand_u64() >> 32);
}

// Uniform in [lo, hi).
inline f32 test_rand_f32(f32 lo, f32 hi) {
    // 24 random bits: exact in an f32, in [0, 1).
    const f32 t = static_cast<f32>(test_rand_u64() >> 40) * (1.0f / 16777216.0f);
    return lo + (hi - lo) * t;
}
