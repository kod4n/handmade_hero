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

static void game_sound(GameAudioBuffer& buffer, GameAudioState& state);

static void game_render(GameOffscreenBuffer& buffer, int green_offset, int blue_offset);
