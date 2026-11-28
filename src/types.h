#pragma once
#include <stdint.h>

// Which compiler is building us, as 0/1 so `#if COMPILER_X` works. Can be
// set on the command line instead. COMPILER_MSVC means an MSVC-compatible
// front end, so it's 1 for clang-cl too (which also sets COMPILER_LLVM):
// that's what the Windows build uses, and it supports the MSVC intrinsics.
#if !defined(COMPILER_MSVC)
#if defined(_MSC_VER)
#define COMPILER_MSVC 1
#else
#define COMPILER_MSVC 0
#endif
#endif

#if !defined(COMPILER_LLVM)
#if defined(__clang__)
#define COMPILER_LLVM 1
#else
#define COMPILER_LLVM 0
#endif
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;

// size literals, e.g. 64_MB, 4_GB
consteval u64 operator""_KB(u64 amount) {
    return amount * 1024;
}
consteval u64 operator""_MB(u64 amount) {
    return amount * 1024_KB;
}
consteval u64 operator""_GB(u64 amount) {
    return amount * 1024_MB;
}
consteval u64 operator""_TB(u64 amount) {
    return amount * 1024_GB;
}
