#include "defer.h"
#include "game.h"

#define NOMINMAX
#include <math.h>
#include <windows.h>
#include <xinput.h>
#include <xaudio2.h>
#include <strsafe.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdarg>
#include <utility>

using namespace HH;

// render constants
static constexpr u32 k_monitor_refresh_hz = 60;
static constexpr u32 k_game_refresh_hz = k_monitor_refresh_hz;

// audio constants
static constexpr u32 k_audio_channels = 2;
static constexpr u32 k_audio_bits_per_channel_sample = 16;
static constexpr u32 k_audio_bytes_per_frame = sizeof(i16) * k_audio_channels;
static_assert(sizeof(AudioFrame) == k_audio_bytes_per_frame);
static constexpr u32 k_sound_samples_per_second = 48000;
static constexpr u32 k_audio_frames_per_buffer = 480;
// Audio is only topped up once per game frame, so the queue must outlast a frame
// plus scheduler jitter. Keep ~2 game frames queued to avoid underruns.
static constexpr u32 k_audio_frames_per_game_frame = k_sound_samples_per_second / k_game_refresh_hz;
static constexpr u32 k_audio_queued_buffer_count =
    (2 * k_audio_frames_per_game_frame + k_audio_frames_per_buffer - 1) / k_audio_frames_per_buffer;
// +1 so the buffer being refilled is never one XAudio2 still reads from.
static constexpr u32 k_audio_buffer_count = k_audio_queued_buffer_count + 1;

static u64 perf_count_freq;

struct Win32OffscreenBuffer {
    BITMAPINFO bitmap_info;
    std::span<u32> pixels;
    i16 width;
    i16 height;
};

struct Win32SoundOutput {
    u32 samples_per_second = k_sound_samples_per_second;
    bool started = false;
    bool enabled = false;
};

struct Win32AudioBuffer {
    std::array<AudioFrame, k_audio_frames_per_buffer> frames;
};

struct Win32Audio {
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* mastering_voice = nullptr;
    IXAudio2SourceVoice* source_voice = nullptr;

    std::array<Win32AudioBuffer, k_audio_buffer_count> buffers;
    u32 next_buffer_idx = 0;
};


static bool running;
static Win32OffscreenBuffer global_backbuffer;
static Win32Audio global_audio;

struct Win32WindowDim {
    int width;
    int height;
};

#define X_INPUT_GET_STATE(name) \
    DWORD WINAPI name(DWORD dwUserIndex [[maybe_unused]], XINPUT_STATE* pState [[maybe_unused]])
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(xinput_get_state_stub) {
    return 0;
}
static x_input_get_state* xinput_get_state_ = &xinput_get_state_stub;
#define XInputGetState xinput_get_state_

#define X_INPUT_SET_STATE(name) \
    DWORD WINAPI name(DWORD dwUserIndex [[maybe_unused]], XINPUT_VIBRATION* pVibration [[maybe_unused]])
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(xinput_set_state_stub) {
    return 0;
}
[[maybe_unused]] static x_input_set_state* xinput_set_state_ = &xinput_set_state_stub;  // for rumble, not wired up yet
#define XInputSetState xinput_set_state_

static void win32_load_xinput() {
    HMODULE lib = LoadLibrary("xinput1_4.dll");
    if (!lib) {
        lib = LoadLibrary("xinput1_3.dll");
    }

    if (lib) {
        xinput_get_state_ = (x_input_get_state*)GetProcAddress(lib, "XInputGetState");
        xinput_set_state_ = (x_input_set_state*)GetProcAddress(lib, "XInputSetState");
    }
}

static void win32_process_kb_button(ActionState& state, bool is_down) {
    state.ended_down = is_down;
    state.half_transition_count++;
}

static void win32_process_xinput_button(
    DWORD button_state,
    DWORD button_bit,
    ActionState& old_state,
    ActionState& new_state
) {
    new_state.ended_down = (button_state & button_bit) == button_bit;
    new_state.half_transition_count = (old_state.ended_down != new_state.ended_down) ? 1 : 0;
}

static f32 win32_normalize_xinput_stick(i16 raw_value, i16 dead_zone) {
    if (raw_value > -dead_zone && raw_value < dead_zone) {
        return 0.0f;
    }
    f32 divisor = raw_value < 0 ? 32768.0f : 32767.0f;
    return raw_value / divisor;
}

static void win32_log_hresult(const char* op, HRESULT hr) {
    char message[1024] = {};

    DWORD length = FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        message,
        static_cast<DWORD>(sizeof(message)),
        nullptr
    );

    char output[2048] = {};

    if (length > 0) {
        // Remove trailing CR/LF from the system message.
        while (length > 0 && (message[length - 1] == '\r' || message[length - 1] == '\n')) {
            message[--length] = '\0';
        }

        snprintf(
            output,
            sizeof(output),
            "[error] %s failed: HRESULT=0x%08lX (%s)\n",
            op,
            static_cast<unsigned long>(hr),
            message
        );
    } else {
        snprintf(
            output,
            sizeof(output),
            "[error] %s failed: HRESULT=0x%08lX "
            "(no system message available)\n",
            op,
            static_cast<unsigned long>(hr)
        );
    }

    OutputDebugString(output);
}

enum class LogSeverity { Info, Warn, Error };

static const char* log_severity(LogSeverity severity) {
    switch (severity) {
        using enum LogSeverity;
    case Info:
        return "info";
    case Warn:
        return "warn";
    case Error:
        return "error";
    }
    return "info";  // should not get here with exhaustive switch
}

// format attribute: clang checks the arguments against the printf format string
[[gnu::format(printf, 2, 3)]] static void win32_log(LogSeverity severity, const char* format, ...) {
    char buffer[1024];
    size_t prefix_length = 0;
    auto severity_str = log_severity(severity);

    HRESULT hr = StringCchPrintfA(buffer, ARRAYSIZE(buffer), "[%s] ", severity_str);
    if (FAILED(hr)) {
        win32_log_hresult("log formatting failed", hr);
        return;
    }

    // Append the formatted message after the prefix.
    prefix_length = strlen(buffer);

    va_list args;
    va_start(args, format);

    hr = StringCchVPrintfA(buffer + prefix_length, ARRAYSIZE(buffer) - prefix_length, format, args);

    va_end(args);
    if (SUCCEEDED(hr)) {
        OutputDebugString(buffer);
    } else {
        win32_log_hresult("debug message formatting failed", hr);
    }
}

#if DEV
static void dev_platform_free_file(void* memory) {
    VirtualFree(memory, 0, MEM_RELEASE);
}

static ReadFileResult dev_platform_read_file(const char* filename) {
    win32_log(LogSeverity::Info, "attempt file read for %s", filename);
    ReadFileResult result = {};
    HANDLE file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        win32_log(LogSeverity::Warn, "failed to read file %s", filename);
        return result;
    }
    defer(CloseHandle(file));

    LARGE_INTEGER file_size_res;
    if (!GetFileSizeEx(file, &file_size_res)) {
        return result;
    }

    u32 file_size = safe_truncate(static_cast<u64>(file_size_res.QuadPart));
    result.content = VirtualAlloc(nullptr, file_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!result.content) {
        return result;
    }

    DWORD bytes_read;
    if (ReadFile(file, result.content, file_size, &bytes_read, nullptr) && file_size == bytes_read) {
        result.size = file_size;
    } else {
        dev_platform_free_file(result.content);
        result.content = nullptr;
    }
    return result;
}

static bool dev_platform_write_file(const char* filename, u32 size, void* memory) {
    bool result = false;
    HANDLE file = CreateFile(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        win32_log(LogSeverity::Warn, "failed to write file %s", filename);
        return result;
    }
    defer(CloseHandle(file));

    DWORD bytes_written;
    if (WriteFile(file, memory, size, &bytes_written, nullptr)) {
        result = bytes_written == size;
    }
    return result;
}
#endif

// Game code loading
//
// The game lives in game.dll. We load a *copy* of it (game_temp.dll) so the
// original stays unlocked and the compiler can overwrite it while we run.
// Each frame we compare game.dll's write time against the one we loaded and
// reload on change. All game memory lives in Storage (owned here), so game
// state survives the swap; only statics inside the DLL are reset.

struct Win32GameCode {
    HMODULE dll;
    FILETIME dll_last_write_time;
    GameRender* render;
    GameSound* sound;
};

struct Win32GameCodePaths {
    char source_dll[MAX_PATH];
    char temp_dll[MAX_PATH];
    char lock[MAX_PATH];  // exists while build.bat is writing game.dll
};

static void game_render_stub(Storage&, ScreenBuffer&, Input&, AudioState&) {}

static void game_sound_stub(AudioBuffer& buffer, AudioState&) {
    // Silence rather than replaying whatever was left in the buffer.
    for (AudioFrame& frame : buffer.frames) {
        frame = {};
    }
}

static FILETIME win32_get_last_write_time(const char* filename) {
    FILETIME result = {};
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesEx(filename, GetFileExInfoStandard, &data)) {
        result = data.ftLastWriteTime;
    }
    return result;
}

static bool win32_file_exists(const char* filename) {
    return GetFileAttributes(filename) != INVALID_FILE_ATTRIBUTES;
}

// Paths are built from the exe's directory, so it doesn't matter what the
// working directory is when the game is launched.
static void win32_get_exe_dir(char (&exe_dir)[MAX_PATH]) {
    DWORD length = GetModuleFileName(nullptr, exe_dir, MAX_PATH);
    // Truncate after the last backslash, keeping it.
    for (DWORD i = length; i > 0; i--) {
        if (exe_dir[i - 1] == '\\') {
            exe_dir[i] = '\0';
            break;
        }
    }
}

static Win32GameCodePaths win32_get_game_code_paths(const char* exe_dir) {
    Win32GameCodePaths paths = {};
    StringCchPrintfA(paths.source_dll, MAX_PATH, "%sgame.dll", exe_dir);
    StringCchPrintfA(paths.temp_dll, MAX_PATH, "%sgame_temp.dll", exe_dir);
    StringCchPrintfA(paths.lock, MAX_PATH, "%slock.tmp", exe_dir);
    return paths;
}

static Win32GameCode win32_load_game_code(const Win32GameCodePaths& paths) {
    Win32GameCode result = {};
    FILETIME write_time = win32_get_last_write_time(paths.source_dll);

    if (CopyFile(paths.source_dll, paths.temp_dll, FALSE)) {
        result.dll = LoadLibrary(paths.temp_dll);
    }
    if (result.dll) {
        result.render = reinterpret_cast<GameRender*>(GetProcAddress(result.dll, "game_render"));
        result.sound = reinterpret_cast<GameSound*>(GetProcAddress(result.dll, "game_sound"));
    }

    if (result.render && result.sound) {
        // Only record the write time on success, so a failed load retries next frame.
        result.dll_last_write_time = write_time;
        win32_log(LogSeverity::Info, "loaded game code\n");
    } else {
        win32_log(LogSeverity::Warn, "failed to load game code, using stubs\n");
        result.render = game_render_stub;
        result.sound = game_sound_stub;
    }
    return result;
}

static void win32_unload_game_code(Win32GameCode& game_code) {
    if (game_code.dll) {
        FreeLibrary(game_code.dll);
    }
    game_code = {};
    game_code.render = game_render_stub;
    game_code.sound = game_sound_stub;
}

// Looped live code editing
//
// Press L to start recording: game memory is snapshotted to a file, then each
// frame's input is appended after it. Press L again to stop recording and loop
// playback: the snapshot is restored and the recorded input is fed back in,
// rewinding to the snapshot whenever it runs out. With hot reload this keeps a
// moment of gameplay repeating while the code that runs it is edited. Press L
// during playback to stop.
//
// Only Storage::main is snapshotted. Transient storage is scratch, and at 4 GB
// too big to write out on every recording.
struct Win32State {
    void* game_memory;
    u64 game_memory_size;

    HANDLE recording_handle;
    u32 recording_idx;  // 0 = not recording
    u32 recorded_frame_count;
    HANDLE playback_handle;
    u32 playback_idx;        // 0 = not playing back
    u32 playback_frame_idx;  // frames played since the last rewind
    u32 playback_frame_count;

    char exe_dir[MAX_PATH];
};

static void win32_get_input_file_path(const Win32State& state, u32 slot_idx, char (&path)[MAX_PATH]) {
    StringCchPrintfA(path, MAX_PATH, "%sloop_edit_%u.hmi", state.exe_dir, slot_idx);
}

static void win32_begin_recording_input(Win32State& state, u32 slot_idx) {
    char path[MAX_PATH];
    win32_get_input_file_path(state, slot_idx, path);
    HANDLE file = CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        win32_log(LogSeverity::Warn, "failed to open %s for recording\n", path);
        return;
    }

    u32 size = safe_truncate(state.game_memory_size);
    DWORD bytes_written;
    if (!WriteFile(file, state.game_memory, size, &bytes_written, nullptr) || bytes_written != size) {
        win32_log(LogSeverity::Warn, "failed to snapshot game memory to %s\n", path);
        CloseHandle(file);
        return;
    }

    state.recording_handle = file;
    state.recording_idx = slot_idx;
    state.recorded_frame_count = 0;
    win32_log(LogSeverity::Info, "recording input to %s\n", path);
}

static void win32_end_recording_input(Win32State& state) {
    CloseHandle(state.recording_handle);
    state.recording_handle = nullptr;
    state.recording_idx = 0;
    win32_log(LogSeverity::Info, "stopped recording input after %u frames\n", state.recorded_frame_count);
}

static void win32_begin_input_playback(Win32State& state, u32 slot_idx) {
    char path[MAX_PATH];
    win32_get_input_file_path(state, slot_idx, path);
    HANDLE file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        win32_log(LogSeverity::Warn, "failed to open %s for playback\n", path);
        return;
    }

    u32 size = safe_truncate(state.game_memory_size);
    DWORD bytes_read;
    if (!ReadFile(file, state.game_memory, size, &bytes_read, nullptr) || bytes_read != size) {
        win32_log(LogSeverity::Warn, "failed to restore game memory from %s\n", path);
        CloseHandle(file);
        return;
    }

    // Whatever follows the snapshot is one Input per recorded frame.
    LARGE_INTEGER file_size;
    u64 input_bytes = 0;
    if (GetFileSizeEx(file, &file_size)) {
        input_bytes = static_cast<u64>(file_size.QuadPart) - state.game_memory_size;
    }

    state.playback_handle = file;
    state.playback_idx = slot_idx;
    state.playback_frame_idx = 0;
    state.playback_frame_count = safe_truncate(input_bytes / sizeof(Input));
}

static void win32_end_input_playback(Win32State& state) {
    CloseHandle(state.playback_handle);
    state.playback_handle = nullptr;
    state.playback_idx = 0;
}

static void win32_record_input(Win32State& state, const Input& input) {
    DWORD bytes_written;
    if (!WriteFile(state.recording_handle, &input, sizeof(input), &bytes_written, nullptr)
        || bytes_written != sizeof(input)) {
        win32_log(LogSeverity::Warn, "failed to record input, stopping recording\n");
        win32_end_recording_input(state);
        return;
    }
    state.recorded_frame_count++;
}

// Returns false if there is nothing to play back, which ends playback.
static bool win32_read_recorded_input(Win32State& state, Input& input) {
    DWORD bytes_read;
    if (!ReadFile(state.playback_handle, &input, sizeof(input), &bytes_read, nullptr) || bytes_read != sizeof(input)) {
        return false;
    }
    state.playback_frame_idx++;
    return true;
}

static void win32_playback_input(Win32State& state, Input& input) {
    if (win32_read_recorded_input(state, input)) {
        return;
    }

    // Out of input: rewind by restoring the snapshot and starting over.
    u32 slot_idx = state.playback_idx;
    win32_end_input_playback(state);
    win32_begin_input_playback(state, slot_idx);
    if (state.playback_idx == 0 || !win32_read_recorded_input(state, input)) {
        // Still nothing, e.g. a recording with no frames in it.
        win32_log(LogSeverity::Warn, "no recorded input to play back\n");
        if (state.playback_idx != 0) {
            win32_end_input_playback(state);
        }
    }
}

enum class LoopMode { Off, Recording, Playback };

static LoopMode win32_loop_mode(const Win32State& state) {
    if (state.playback_idx != 0) {
        return LoopMode::Playback;
    }
    if (state.recording_idx != 0) {
        return LoopMode::Recording;
    }
    return LoopMode::Off;
}

static const char* win32_window_title(LoopMode mode) {
    switch (mode) {
        using enum LoopMode;
    case Off:
        return "Handmade Hero";
    case Recording:
        return "Handmade Hero [recording loop]";
    case Playback:
        return "Handmade Hero [looping playback]";
    }
    return "Handmade Hero";  // should not get here with exhaustive switch
}

// Fills [x_min, x_max) x [y_min, y_max), clipped to the backbuffer. The game has its own
// version, but it lives in game.dll, so the platform can't call it.
static void win32_fill_rect(Win32OffscreenBuffer& backbuffer, int x_min, int y_min, int x_max, int y_max, u32 color) {
    x_min = std::clamp(x_min, 0, static_cast<int>(backbuffer.width));
    x_max = std::clamp(x_max, 0, static_cast<int>(backbuffer.width));
    y_min = std::clamp(y_min, 0, static_cast<int>(backbuffer.height));
    y_max = std::clamp(y_max, 0, static_cast<int>(backbuffer.height));

    const size_t pitch = static_cast<size_t>(backbuffer.width);
    for (int y = y_min; y < y_max; y++) {
        std::span<u32> row = backbuffer.pixels.subspan(static_cast<size_t>(y) * pitch, pitch);
        for (int x = x_min; x < x_max; x++) {
            row[static_cast<size_t>(x)] = color;
        }
    }
}

// Drawn over the game's frame: a badge in the top-left corner (red while
// recording, green while playing back), and during playback a bar along the
// top edge showing how far through the loop we are.
static void win32_draw_loop_overlay(Win32OffscreenBuffer& backbuffer, const Win32State& state) {
    constexpr int badge_margin = 8;
    constexpr int badge_size = 16;
    constexpr int bar_height = 4;
    constexpr u32 recording_color = 0x00FF2020;
    constexpr u32 playback_color = 0x0020FF20;
    constexpr u32 bar_track_color = 0x00202020;

    switch (win32_loop_mode(state)) {
        using enum LoopMode;
    case Off: {
    } break;

    case Recording: {
        // Blink about once a second so it reads as "live".
        if ((state.recorded_frame_count / (k_game_refresh_hz / 2)) % 2 == 0) {
            win32_fill_rect(
                backbuffer,
                badge_margin,
                badge_margin,
                badge_margin + badge_size,
                badge_margin + badge_size,
                recording_color
            );
        }
    } break;

    case Playback: {
        win32_fill_rect(
            backbuffer,
            badge_margin,
            badge_margin,
            badge_margin + badge_size,
            badge_margin + badge_size,
            playback_color
        );

        win32_fill_rect(backbuffer, 0, 0, backbuffer.width, bar_height, bar_track_color);
        if (state.playback_frame_count > 0) {
            u64 progress = static_cast<u64>(backbuffer.width) * state.playback_frame_idx;
            int bar_width = static_cast<int>(progress / state.playback_frame_count);
            win32_fill_rect(backbuffer, 0, 0, bar_width, bar_height, playback_color);
        }
    } break;
    }
}

static void win32_toggle_input_loop(Win32State& state) {
    if (state.playback_idx != 0) {
        win32_end_input_playback(state);
        win32_log(LogSeverity::Info, "stopped input playback\n");
    } else if (state.recording_idx == 0) {
        win32_begin_recording_input(state, 1);
    } else {
        win32_end_recording_input(state);
        win32_begin_input_playback(state, 1);
        if (state.playback_idx != 0) {
            win32_log(LogSeverity::Info, "looping input playback\n");
        }
    }
}

static bool win32_init_xaudio2(Win32SoundOutput& sound_output) {
    HRESULT hr = XAudio2Create(&global_audio.engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        win32_log_hresult("creating XAudio2 engine", hr);
        return false;
    }

    hr = global_audio.engine->CreateMasteringVoice(&global_audio.mastering_voice);
    if (FAILED(hr)) {
        win32_log_hresult("creating XAudio2 mastering voice", hr);
        return false;
    }

    WAVEFORMATEX wave_fmt = {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = k_audio_channels,
        .nSamplesPerSec = sound_output.samples_per_second,
        .nBlockAlign = k_audio_bytes_per_frame,
        .wBitsPerSample = k_audio_bits_per_channel_sample,
    };
    wave_fmt.nAvgBytesPerSec = wave_fmt.nSamplesPerSec * wave_fmt.nBlockAlign;

    hr = global_audio.engine->CreateSourceVoice(&global_audio.source_voice, &wave_fmt);
    if (FAILED(hr)) {
        win32_log_hresult("creating XAudio2 source voice", hr);
        return false;
    }

    global_audio.next_buffer_idx = 0;
    sound_output.started = false;

    return true;
}

static bool fill_sound_buffer(
    GameSound* game_sound,
    Win32SoundOutput& sound_output,
    AudioState& audio_state,
    u32 buffer_idx
) {
    if (buffer_idx >= k_audio_buffer_count || !global_audio.source_voice) {
        return false;
    }

    Win32AudioBuffer& audio_buffer = global_audio.buffers[buffer_idx];

    AudioBuffer game_buffer = {
        .frames = audio_buffer.frames,
        .samples_per_second = sound_output.samples_per_second,
    };
    game_sound(game_buffer, audio_state);

    XAUDIO2_BUFFER buffer = {};
    buffer.AudioBytes = static_cast<u32>(sizeof(audio_buffer.frames));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(audio_buffer.frames.data());

    // No END_OF_STREAM: this is a continuous stream.
    buffer.Flags = 0;

    HRESULT hr = global_audio.source_voice->SubmitSourceBuffer(&buffer);

    if (FAILED(hr)) {
        win32_log_hresult("submitting XAudio2 buffer", hr);
        return false;
    }

    return true;
}

static bool process_audio(GameSound* game_sound, Win32SoundOutput& sound_output, AudioState& audio_state) {
    if (!global_audio.source_voice || !sound_output.enabled) {
        return false;
    }

    XAUDIO2_VOICE_STATE state = {};
    global_audio.source_voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

    while (state.BuffersQueued < k_audio_queued_buffer_count) {
        const u32 buffer_idx = global_audio.next_buffer_idx;

        if (!fill_sound_buffer(game_sound, sound_output, audio_state, buffer_idx)) {
            return false;
        }

        global_audio.next_buffer_idx = (global_audio.next_buffer_idx + 1) % k_audio_buffer_count;

        // Account for the newly submitted buffer.
        ++state.BuffersQueued;
    }

    // Start playback after the initial queue has been populated.
    if (!sound_output.started) {
        HRESULT hr = global_audio.source_voice->Start();
        if (FAILED(hr)) {
            win32_log_hresult("starting XAudio2 voice", hr);
            return false;
        }

        sound_output.started = true;
    }

    return true;
}

Win32WindowDim get_window_dim(HWND window) {
    RECT rect;
    GetClientRect(window, &rect);
    Win32WindowDim result = {
        .width = rect.right - rect.left,
        .height = rect.bottom - rect.top,
    };
    return result;
}

static void win32_resize_dib_section(Win32OffscreenBuffer* backbuffer, int w, int h) {
    if (backbuffer->pixels.data()) {
        VirtualFree(backbuffer->pixels.data(), 0, MEM_RELEASE);
    }

    backbuffer->width = w;
    backbuffer->height = h;
    backbuffer->bitmap_info = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = backbuffer->width,
            .biHeight = -backbuffer->height,
            .biPlanes = 1,
            .biBitCount = 32,  // one u32 per pixel
            .biCompression = BI_RGB,
        }
    };

    size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h);
    void* memory = VirtualAlloc(nullptr, pixel_count * sizeof(u32), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    backbuffer->pixels = {static_cast<u32*>(memory), memory ? pixel_count : 0};
}

static void win32_display_window_buffer(
    HDC device_ctx,
    Win32OffscreenBuffer backbuffer,
    int window_width,
    int window_height
) {
    StretchDIBits(
        device_ctx,
        0,
        0,
        window_width,
        window_height,
        0,
        0,
        backbuffer.width,
        backbuffer.height,
        backbuffer.pixels.data(),
        &backbuffer.bitmap_info,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

static void win32_process_message(ControllerInput& kb, Win32State& state) {
    MSG msg = {};
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
        switch (msg.message) {
        case WM_SYSKEYUP:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_KEYDOWN: {
            u32 vk_code = static_cast<u32>(msg.wParam);
            bool was_down = (msg.lParam & (1 << 30)) != 0;
            bool is_down = (msg.lParam & (1 << 31)) == 0;
            if (was_down == is_down) {
                break;
            }

            switch (vk_code) {
            case VK_F4: {
                bool is_alt_down = msg.lParam & (1 << 29);
                if (is_alt_down) {
                    running = false;
                }
            } break;

            case 'W': {
                win32_process_kb_button(input_action(kb, Action::MoveUp), is_down);
            } break;

            case 'A': {
                win32_process_kb_button(input_action(kb, Action::MoveLeft), is_down);
            } break;

            case 'S': {
                win32_process_kb_button(input_action(kb, Action::MoveDown), is_down);
            } break;

            case 'D': {
                win32_process_kb_button(input_action(kb, Action::MoveRight), is_down);
            } break;

            case 'Q': {
                win32_process_kb_button(input_action(kb, Action::LeftShoulder), is_down);
            } break;

            case 'E': {
                win32_process_kb_button(input_action(kb, Action::RightShoulder), is_down);
            } break;

            case 'L': {
                if (is_down) {
                    win32_toggle_input_loop(state);
                }
            } break;
            }
        } break;
        default: {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } break;
        }
    }
}

LRESULT CALLBACK window_callback(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;
    switch (message) {
    case WM_ACTIVATEAPP: {
        OutputDebugStringA("WM_ACTIVATEAPP\n");
        break;
    }
    case WM_DESTROY: {
        OutputDebugStringA("WM_DESTROY\n");
        running = false;
        break;
    }
    case WM_CLOSE: {
        OutputDebugStringA("WM_CLOSE\n");
        running = false;
        break;
    }
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
        assert(!"no keyboard messages");
        break;
    }
    default: {
        result = DefWindowProc(window, message, w_param, l_param);
        break;
    }
    }
    return result;
}

inline LARGE_INTEGER win32_wall_clock() {
    LARGE_INTEGER result;
    QueryPerformanceCounter(&result);
    return result;
}

inline f32 win32_seconds_elapsed(LARGE_INTEGER start, LARGE_INTEGER end) {
    f32 elapsed = static_cast<f32>(end.QuadPart - start.QuadPart) / static_cast<f32>(perf_count_freq);
    return elapsed;
}

int CALLBACK WinMain(
    HINSTANCE h_instance,
    [[maybe_unused]] HINSTANCE h_prev_instance,
    [[maybe_unused]] LPSTR lp_cmd_line,
    [[maybe_unused]] int n_cmd_show
) {
    LARGE_INTEGER perf_freq_res;
    QueryPerformanceFrequency(&perf_freq_res);
    perf_count_freq = perf_freq_res.QuadPart;

    UINT desired_sched_ms = 1;
    bool sched_is_granular = timeBeginPeriod(desired_sched_ms) == TIMERR_NOERROR;
    win32_log(LogSeverity::Info, "scheduler is granular? %s\n", sched_is_granular ? "true" : "false");

    WNDCLASS window_class = {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = window_callback,
        .hInstance = h_instance,
        .lpszClassName = "HandmadeHeroWindowClass",
    };

    win32_resize_dib_section(&global_backbuffer, 1280, 720);

    if (RegisterClass(&window_class)) {
        HWND window_handle = CreateWindowEx(
            0,
            window_class.lpszClassName,
            "Handmade Hero",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            0,
            0,
            h_instance,
            0
        );
        if (window_handle) {
            running = true;
            win32_load_xinput();

            Win32SoundOutput sound_output = {};
            AudioState audio_state = {};
            if (!win32_init_xaudio2(sound_output)) {
                win32_log(LogSeverity::Error, "failed to init XAudio2\n");
                running = false;
            }

#if DEV
            LPVOID base_address = reinterpret_cast<LPVOID>(2_TB);
#else
            LPVOID base_address = nullptr;
#endif

            Storage storage = {
                .main_size = 64_MB,
                .transient_size = 4_GB,
            };
            u64 total_storage_size = storage.main_size + storage.transient_size;
            storage.main = VirtualAlloc(base_address, total_storage_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            storage.transient = static_cast<u8*>(storage.main) + storage.main_size;

            if (!storage.main) {
                win32_log(LogSeverity::Error, "failed to allocate main game storage");
                return 1;
            }
#if DEV
            storage.dev_read_file = dev_platform_read_file;
            storage.dev_free_file = dev_platform_free_file;
            storage.dev_write_file = dev_platform_write_file;
#endif

            Win32State win32_state = {
                .game_memory = storage.main,
                .game_memory_size = storage.main_size,
            };
            win32_get_exe_dir(win32_state.exe_dir);

            const Win32GameCodePaths game_code_paths = win32_get_game_code_paths(win32_state.exe_dir);
            Win32GameCode game_code = win32_load_game_code(game_code_paths);

            u64 last_cycle_count = __rdtsc();
            LARGE_INTEGER last_counter = win32_wall_clock();

            f32 target_seconds_per_frame = 1.0f / static_cast<f32>(k_game_refresh_hz);

            Input input[2] = {};
            Input* curr_input = &input[0];
            Input* prev_input = &input[1];
            // Played back input goes here rather than over curr_input, so the
            // live keyboard state is still accurate once playback stops.
            Input playback_input = {};
            LoopMode last_loop_mode = LoopMode::Off;

            while (running) {
                // Hot reload: swap in the new DLL once the build has finished writing it.
                FILETIME dll_write_time = win32_get_last_write_time(game_code_paths.source_dll);
                if (CompareFileTime(&dll_write_time, &game_code.dll_last_write_time) != 0
                    && !win32_file_exists(game_code_paths.lock)) {
                    win32_unload_game_code(game_code);
                    game_code = win32_load_game_code(game_code_paths);
                }

                ControllerInput* kb = &curr_input->controllers[0];
                ControllerInput* prev_kb = &prev_input->controllers[0];
                *kb = {};
                kb->is_connected = true;
                for (u8 i = 0; i < ARRAYSIZE(kb->actions); i++) {
                    kb->actions[i].ended_down = prev_kb->actions[i].ended_down;
                }
                win32_process_message(*kb, win32_state);

                constexpr DWORD max_controllers = std::min<DWORD>(XUSER_MAX_COUNT, k_max_controllers);

                for (DWORD idx = 0; idx < max_controllers - 1; idx++) {
                    ControllerInput* new_input = &curr_input->controllers[idx + 1];
                    ControllerInput* old_input = &prev_input->controllers[idx + 1];

                    XINPUT_STATE controller_state;
                    if (XInputGetState(idx, &controller_state) == ERROR_SUCCESS) {
                        XINPUT_GAMEPAD* pad = &controller_state.Gamepad;
                        new_input->is_analog = true;
                        new_input->is_connected = true;

                        // stick
                        new_input->stick_avg_x =
                            win32_normalize_xinput_stick(pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                        new_input->stick_avg_y =
                            win32_normalize_xinput_stick(pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

                        // dpad, overrides joystick when used
                        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) {
                            new_input->is_analog = false;
                            new_input->stick_avg_y = 1.0f;
                        }
                        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) {
                            new_input->is_analog = false;
                            new_input->stick_avg_y = -1.0f;
                        }
                        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
                            new_input->is_analog = false;
                            new_input->stick_avg_x = 1.0f;
                        }
                        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
                            new_input->is_analog = false;
                            new_input->stick_avg_x = -1.0f;
                        }

                        f32 move_threshold = 0.5f;
                        win32_process_xinput_button(
                            (new_input->stick_avg_x < -move_threshold) ? 1 : 0,
                            1,
                            input_action(*old_input, Action::MoveLeft),
                            input_action(*new_input, Action::MoveLeft)
                        );
                        win32_process_xinput_button(
                            (new_input->stick_avg_x > move_threshold) ? 1 : 0,
                            1,
                            input_action(*old_input, Action::MoveRight),
                            input_action(*new_input, Action::MoveRight)
                        );
                        win32_process_xinput_button(
                            (new_input->stick_avg_y < -move_threshold) ? 1 : 0,
                            1,
                            input_action(*old_input, Action::MoveDown),
                            input_action(*new_input, Action::MoveDown)
                        );
                        win32_process_xinput_button(
                            (new_input->stick_avg_y > move_threshold) ? 1 : 0,
                            1,
                            input_action(*old_input, Action::MoveUp),
                            input_action(*new_input, Action::MoveUp)
                        );

                        win32_log(
                            LogSeverity::Info,
                            "stick info: x = %.02f | y = %.02f | threshold = %.02f\n",
                            new_input->stick_avg_x,
                            new_input->stick_avg_y,
                            move_threshold
                        );

                        // buttons
                        win32_process_xinput_button(
                            pad->wButtons,
                            XINPUT_GAMEPAD_Y,
                            input_action(*old_input, Action::ActionUp),
                            input_action(*new_input, Action::ActionUp)
                        );
                        win32_process_xinput_button(
                            pad->wButtons,
                            XINPUT_GAMEPAD_A,
                            input_action(*old_input, Action::ActionDown),
                            input_action(*new_input, Action::ActionDown)
                        );
                        win32_process_xinput_button(
                            pad->wButtons,
                            XINPUT_GAMEPAD_X,
                            input_action(*old_input, Action::ActionLeft),
                            input_action(*new_input, Action::ActionLeft)
                        );
                        win32_process_xinput_button(
                            pad->wButtons,
                            XINPUT_GAMEPAD_B,
                            input_action(*old_input, Action::ActionRight),
                            input_action(*new_input, Action::ActionRight)
                        );
                        win32_process_xinput_button(
                            pad->wButtons,
                            XINPUT_GAMEPAD_LEFT_SHOULDER,
                            input_action(*old_input, Action::LeftShoulder),
                            input_action(*new_input, Action::LeftShoulder)
                        );
                        win32_process_xinput_button(
                            pad->wButtons,
                            XINPUT_GAMEPAD_RIGHT_SHOULDER,
                            input_action(*old_input, Action::RightShoulder),
                            input_action(*new_input, Action::RightShoulder)
                        );
                    } else {
                        new_input->is_connected = false;
                    }
                }

                ScreenBuffer offscreen_buffer = {
                    .pixels = global_backbuffer.pixels,
                    .width = static_cast<u32>(global_backbuffer.width),
                    .height = static_cast<u32>(global_backbuffer.height),
                    .pitch = static_cast<u32>(global_backbuffer.width),
                };

                Input* game_input = curr_input;
                if (win32_state.recording_idx != 0) {
                    win32_record_input(win32_state, *curr_input);
                }
                if (win32_state.playback_idx != 0) {
                    win32_playback_input(win32_state, playback_input);
                    if (win32_state.playback_idx != 0) {
                        game_input = &playback_input;
                    }
                }

                game_code.render(storage, offscreen_buffer, *game_input, audio_state);
                process_audio(game_code.sound, sound_output, audio_state);

                win32_draw_loop_overlay(global_backbuffer, win32_state);
                LoopMode loop_mode = win32_loop_mode(win32_state);
                if (loop_mode != last_loop_mode) {
                    SetWindowText(window_handle, win32_window_title(loop_mode));
                    last_loop_mode = loop_mode;
                }

                HDC device_ctx = GetDC(window_handle);
                Win32WindowDim window_dim = get_window_dim(window_handle);
                win32_display_window_buffer(device_ctx, global_backbuffer, window_dim.width, window_dim.height);
                ReleaseDC(window_handle, device_ctx);

                LARGE_INTEGER work_counter = win32_wall_clock();
                f32 frame_seconds = win32_seconds_elapsed(last_counter, work_counter);
                f32 frame_wait = frame_seconds;
                if (frame_seconds < target_seconds_per_frame) {
                    while (frame_wait < target_seconds_per_frame) {
                        if (sched_is_granular) {
                            DWORD sleep_ms = static_cast<DWORD>(1000.0f * (target_seconds_per_frame - frame_wait));
                            if (sleep_ms > 0) {
                                Sleep(sleep_ms);
                            }
                        }
                        frame_wait = win32_seconds_elapsed(last_counter, win32_wall_clock());
                    }
                } else {
                    win32_log(
                        LogSeverity::Warn,
                        "frame rate [%.02f] missed target frame rate [%.02f]\n",
                        frame_seconds,
                        target_seconds_per_frame
                    );
                }

                LARGE_INTEGER end_counter = win32_wall_clock();
                f32 ms_per_frame = 1000.0f * win32_seconds_elapsed(last_counter, end_counter);
                last_counter = end_counter;

                u64 end_cycle_count = __rdtsc();
                i64 cycles_elapsed = end_cycle_count - last_cycle_count;
                last_cycle_count = end_cycle_count;
                f32 fps = 0.0f;
                // static_cast<f32>(static_cast<f32>(perf_count_freq) / static_cast<f32>(counter_elapsed));
                f32 mcpf = static_cast<f32>(static_cast<f32>(cycles_elapsed) / (1000.0f * 1000.0f));

                win32_log(LogSeverity::Info, "%.02fms/f | %.02ffps | %.02fmcpf\n", ms_per_frame, fps, mcpf);

                std::swap(curr_input, prev_input);
            }
        } else {
            // TODO logging
        }
    } else {
        // TODO logging
    }
}
