#pragma once

#include "types.h"
#include <bit>
#include <cassert>
#include <span>
#include <type_traits>

// Linear allocator over a fixed block of memory. Memory is never freed
// individually; reset `used` to release everything at once.
struct Arena {
    u8* base;
    u64 size;
    u64 used;
};

// Arena memory is raw bytes with no constructor run, so only types that can
// begin their lifetime that way may be pushed.
template <typename T>
concept ArenaType = std::is_trivially_copyable_v<T>;

inline Arena arena_make(void* memory, u64 size) {
    return {.base = static_cast<u8*>(memory), .size = size, .used = 0};
}

inline void* arena_push_bytes(Arena& arena, u64 size, u64 align) {
    assert(std::has_single_bit(align));
    uintptr_t current = reinterpret_cast<uintptr_t>(arena.base + arena.used);
    uintptr_t aligned = (current + (align - 1)) & ~(align - 1);
    u64 offset = aligned - reinterpret_cast<uintptr_t>(arena.base);
    assert(offset + size <= arena.size);
    arena.used = offset + size;
    return arena.base + offset;
}

template <ArenaType T>
T* arena_push(Arena& arena) {
    return static_cast<T*>(arena_push_bytes(arena, sizeof(T), alignof(T)));
}

template <ArenaType T>
std::span<T> arena_push_array(Arena& arena, u64 count) {
    return {static_cast<T*>(arena_push_bytes(arena, sizeof(T) * count, alignof(T))), count};
}
