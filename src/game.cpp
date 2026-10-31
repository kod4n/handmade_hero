#include "types.h"
#include "math.h"
#include "game.h"

static u32 pack_color(u8 r, u8 g, u8 b, u8 a = 0) {
    return (static_cast<u32>(a) << 24 | static_cast<u32>(r) << 16 | static_cast<u32>(g) << 8 | static_cast<u32>(b));
}

static void render_gradient(GameOffscreenBuffer& buffer, int green_offset, int blue_offset) {
    u8* row = static_cast<u8*>(buffer.memory);
    for (int y = 0; y < buffer.height; y++) {
        u32* pixel = reinterpret_cast<u32*>(row);
        for (int x = 0; x < buffer.width; x++) {
            *pixel++ = pack_color(0, x + green_offset, y + blue_offset);
        }
        row += buffer.pitch;
    }
}

static void game_sound(GameAudioBuffer& audio, GameAudioState& state) {
    const f32 phase_step = 2.0f * k_pi * state.tone_hz / static_cast<f32>(audio.samples_per_second);

    i16* sample_out = audio.samples;
    for (u32 frame = 0; frame < audio.sample_count; ++frame) {
        f32 sine = sinf(state.t_sine);
        i16 sample = static_cast<i16>(sine * state.tone_volume);
        *sample_out++ = sample;
        *sample_out++ = sample;
        state.t_sine += phase_step;
    }

    if (state.t_sine >= 2.0f * k_pi) {
        state.t_sine -= 2.0f * k_pi;
    }
}

static void game_render(GameOffscreenBuffer& buffer, GameInput& input, GameAudioState& audio_state) {
    // move these into a GameState struct at some point, or just remove
    static int green_offset = 0;
    static int blue_offset = 0;

    GameControllerInput* controller = &input.controllers[0];
    if (controller->is_analog) {  // controller
        audio_state.tone_hz = 256 + static_cast<int>(128.0f * controller->end_x);
        green_offset += static_cast<int>(4.0f * controller->end_x);
        blue_offset += static_cast<int>(4.0f * controller->end_y);
    } else {  // mkb
    }

    render_gradient(buffer, green_offset, blue_offset);
}
