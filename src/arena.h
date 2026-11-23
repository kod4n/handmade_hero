#pragma once

#include "types.h"
#include <bit>
#include <cassert>
#include <span>

namespace hh {

// Arena memory is raw bytes with no constructor run, so only types that can
// begin their lifetime that way may be pushed.
template <typename T>
concept ArenaType = std::is_trivially_copyable_v<T>;

// Linear allocator over a fixed block of memory. Memory is never freed
// individually; reset `used` to release everything at once.
struct Arena {
    u8* base;
    u64 size;
    u64 used;

    static Arena make(void* memory, u64 size) {
        return {.base = static_cast<u8*>(memory), .size = size, .used = 0};
    }

    template <ArenaType T>
    T* push() {
        return static_cast<T*>(push_bytes(sizeof(T), alignof(T)));
    }

    template <ArenaType T>
    std::span<T> push_array(u64 count) {
        return {static_cast<T*>(push_bytes(sizeof(T) * count, alignof(T))), count};
    }

private:
    void* push_bytes(u64 bytes, u64 align) {
        assert(std::has_single_bit(align));
        uintptr_t current = reinterpret_cast<uintptr_t>(base + used);
        uintptr_t aligned = (current + (align - 1)) & ~(align - 1);
        u64 offset = aligned - reinterpret_cast<uintptr_t>(base);
        assert(offset + bytes <= size);
        used = offset + bytes;
        return base + offset;
    }
};

}  // namespace hh
