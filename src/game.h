#pragma once

#include "types.h"
#include "arena.h"
#include <cassert>
#include <numbers>
#include <span>

namespace HH {

inline constexpr f32 k_pi = std::numbers::pi_v<f32>;
inline constexpr u32 k_max_controllers = 4;

#if DEV
struct ReadFileResult {
    u32 size;
    void* content;
};

// Platform services the game calls. The game lives in its own DLL on Windows,
// so it can't link against platform functions directly; they're passed in here.
using DevPlatformReadFile = ReadFileResult(const char* filename);
using DevPlatformFreeFile = void(void* memory);
using DevPlatformWriteFile = bool(const char* filename, u32 size, void* memory);
#endif

// Owned by the platform layer, so it survives game code reloads.
struct Storage {
    bool is_initialized;
    u64 main_size;
    void* main;
    u64 transient_size;
    void* transient;

#if DEV
    DevPlatformReadFile* dev_read_file;
    DevPlatformFreeFile* dev_free_file;
    DevPlatformWriteFile* dev_write_file;
#endif
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
    int x_offset;
    int y_offset;
    int player_x;  // top-left corner, in pixels
    int player_y;
};
static_assert(ArenaType<GameState>);

struct ActionState {
    i32 half_transition_count;
    bool ended_down;
};

enum class Action : u8 {
    ActionUp,
    ActionDown,
    ActionLeft,
    ActionRight,
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    LeftShoulder,
    RightShoulder,
    Count,
};

struct ControllerInput {
    bool is_analog;
    bool is_connected;

    // stick positions
    f32 stick_avg_x;
    f32 stick_avg_y;

    ActionState actions[static_cast<u8>(Action::Count)];
};

inline ActionState& input_action(ControllerInput& controller, Action a) {
    assert(a < Action::Count);
    return controller.actions[static_cast<u8>(a)];
}

inline const ActionState& input_action(const ControllerInput& controller, Action a) {
    assert(a < Action::Count);
    return controller.actions[static_cast<u8>(a)];
}

struct Input {
    ControllerInput controllers[k_max_controllers];
};

// Game entry points. On Windows they're exported from game.dll with C linkage
// (unmangled names) and looked up with GetProcAddress, so the platform only
// ever refers to them through these function types.
using GameSound = void(AudioBuffer& buffer, AudioState& state);
using GameRender = void(Storage& storage, ScreenBuffer& buffer, Input& input, AudioState& audio_state);

inline u32 safe_truncate(u64 value) {
    assert(value <= UINT32_MAX);
    return static_cast<u32>(value);
}
}  // namespace HH
