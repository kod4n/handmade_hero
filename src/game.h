#pragma once

#include "types.h"

static constexpr f32 k_pi = 3.141592653459f;

struct GameOffscreenBuffer {
    void* memory;
    int width;
    int height;
    int pitch;
};

struct GameAudioBuffer {
    i16* samples;
    u32 sample_count;
    u32 samples_per_second;
};

struct GameAudioState {
    f32 t_sine = 0.0f;
    f32 tone_hz = 256.0f;
    f32 tone_volume = 500.0f;
};

struct GameButtonState {
    i32 half_transition_count;
    bool ended_down;
};

struct GameControllerInput {
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
        GameButtonState buttons[6];
        struct {
            GameButtonState u;
            GameButtonState d;
            GameButtonState l;
            GameButtonState r;
            GameButtonState ls;
            GameButtonState rs;
        };
    };
};

struct GameInput {
    GameControllerInput controllers[4];
};

static void game_sound(GameAudioBuffer& buffer, GameAudioState& state);

static void game_render(GameOffscreenBuffer& buffer);
