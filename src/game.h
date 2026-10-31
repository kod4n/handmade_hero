#pragma once

#include "types.h"

static constexpr f32 k_pi = 3.141592653459f;

struct Storage {
    bool is_initialized;
    u64 main_size;
    void* main;
    u64 transient_size;
    void* transient;
};

struct ScreenBuffer {
    void* memory;
    int width;
    int height;
    int pitch;
};

struct AudioBuffer {
    i16* samples;
    u32 sample_count;
    u32 samples_per_second;
};

struct AudioState {
    f32 t_sine = 0.0f;
    f32 tone_hz = 256.0f;
    f32 tone_volume = 500.0f;
};

struct GameState {
    int green_offset;
    int blue_offset;
};

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
    ControllerInput controllers[4];
};

static void game_sound(AudioBuffer& buffer, AudioState& state);

static void game_render(Storage& storage, ScreenBuffer& buffer, Input& input, AudioState& audio_state);

static constexpr u32 k_kilobyte = 1024;

static u64 size_kb(u8 amount) {
    return amount * k_kilobyte;
}
static u64 size_mb(u8 amount) {
    return size_kb(amount) * k_kilobyte;
}
static u64 size_gb(u8 amount) {
    return size_mb(amount) * k_kilobyte;
}
static u64 size_tb(u8 amount) {
    return size_gb(amount) * k_kilobyte;
}
