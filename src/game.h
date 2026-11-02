#pragma once

#include "types.h"
#include "arena.h"
#include <cassert>
#include <numbers>
#include <span>

inline constexpr f32 k_pi = std::numbers::pi_v<f32>;
inline constexpr u32 k_max_controllers = 4;

struct Storage {
    bool is_initialized;
    u64 main_size;
    void* main;
    u64 transient_size;
    void* transient;
};

struct ScreenBuffer {
    std::span<u32> pixels;  // 0xAARRGGBB, top-down rows
    u32 width;
    u32 height;
    u32 pitch;  // pixels per row
};

// One interleaved stereo frame.
struct AudioFrame {
    i16 left;
    i16 right;
};

struct AudioBuffer {
    std::span<AudioFrame> frames;
    u32 samples_per_second;
};

struct AudioState {
    f32 t_sine = 0.0f;
    f32 tone_hz = 256.0f;
    f32 tone_volume = 500.0f;
};

// Always the first allocation in Storage::main.
struct GameState {
    Arena permanent_arena;  // remainder of Storage::main after GameState
    int green_offset;
    int blue_offset;
};
static_assert(ArenaType<GameState>);

struct ButtonState {
    i32 half_transition_count;
    bool ended_down;
};

struct ControllerInput {
    bool is_analog;

    // stick positions
    f32 start_x;
    f32 start_y;
    f32 end_x;
    f32 end_y;
    f32 max_x;
    f32 max_y;
    f32 min_x;
    f32 min_y;

    union {
        ButtonState buttons[6];
        struct {
            ButtonState u;
            ButtonState d;
            ButtonState l;
            ButtonState r;
            ButtonState ls;
            ButtonState rs;
        };
    };
};

struct Input {
    ControllerInput controllers[k_max_controllers];
};

void game_sound(AudioBuffer& buffer, AudioState& state);

void game_render(Storage& storage, ScreenBuffer& buffer, Input& input, AudioState& audio_state);

inline u32 safe_truncate(u64 value) {
    assert(value <= UINT32_MAX);
    return static_cast<u32>(value);
}

#if DEV
struct ReadFileResult {
    u32 size;
    void* content;
};

ReadFileResult dev_platform_read_file(const char* filename);
void dev_platform_free_file(void* memory);
bool dev_platform_write_file(const char* filename, u32 size, void* memory);
#endif
