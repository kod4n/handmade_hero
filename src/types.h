#pragma once
#include <stdint.h>

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

// useful macros
#define min(a, b) (((a) < (b)) ? (a) : (b))
