#include "game.cpp"
#include "game.h"

#include <math.h>
#include <windows.h>
#include <xinput.h>
#include <xaudio2.h>
#include <strsafe.h>
#include <array>
#include <cstdarg>
#include <vector>

// constants
// audio
static constexpr u32 k_audio_channels = 2;
static constexpr u32 k_audio_bits_per_channel_sample = 16;
static constexpr u32 k_audio_bytes_per_frame = sizeof(i16) * k_audio_channels;
static constexpr int k_audio_frames_per_buffer = 480;
static constexpr u32 k_audio_buffer_size_bytes = k_audio_frames_per_buffer * k_audio_bytes_per_frame;
static constexpr u32 k_audio_buffer_count = 4;
static constexpr u32 k_audio_queued_buffer_count = 3;
static constexpr u32 k_sound_samples_per_second = 48000;

// screen buffer
static constexpr int k_bytes_per_pixel = 4;

struct Win32OffscreenBuffer {
    BITMAPINFO bitmap_info;
    void* memory;
    int width;
    int height;
    int bytes_per_pixel;
    int pitch;
};

struct Win32SoundOutput {
    u32 samples_per_second = k_sound_samples_per_second;
    u32 buffer_size_bytes = k_audio_buffer_size_bytes;
    bool started = false;
    bool enabled = true;
};

struct Win32AudioBuffer {
    std::vector<i16> samples;
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

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(xinput_get_state_stub) {
    return 0;
}
static x_input_get_state* xinput_get_state_ = &xinput_get_state_stub;
#define XInputGetState xinput_get_state_

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(xinput_set_state_stub) {
    return 0;
}
static x_input_set_state* xinput_set_state_ = &xinput_set_state_stub;
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
    const char* severity_str = "";

    switch (severity) {
    case LogSeverity::Info:
        severity_str = "info";
        break;
    case LogSeverity::Warn:
        severity_str = "warn";
        break;
    case LogSeverity::Error:
        severity_str = "error";
        break;
    }

    return severity_str;
}

static void win32_log(LogSeverity severity, const char* format, ...) {
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

    // Allocate every buffer before playback starts.
    const u32 frame_count = sound_output.buffer_size_bytes / k_audio_bytes_per_frame;

    for (Win32AudioBuffer& buffer : global_audio.buffers) {
        buffer.samples.resize(frame_count * k_audio_channels);
    }

    global_audio.next_buffer_idx = 0;
    sound_output.started = false;

    return true;
}

static bool fill_sound_buffer(Win32SoundOutput& sound_output, GameAudioState& audio_state, u32 buffer_idx) {
    if (buffer_idx >= k_audio_buffer_count || !global_audio.source_voice) {
        return false;
    }

    Win32AudioBuffer& audio_buffer = global_audio.buffers[buffer_idx];

    GameAudioBuffer game_buffer = {
        .samples = audio_buffer.samples.data(),
        .sample_count = static_cast<u32>(audio_buffer.samples.size() / k_audio_channels),
        .samples_per_second = sound_output.samples_per_second,
    };
    game_sound(game_buffer, audio_state);

    XAUDIO2_BUFFER buffer = {};
    buffer.AudioBytes = static_cast<u32>(audio_buffer.samples.size() * sizeof(i16));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(audio_buffer.samples.data());

    // No END_OF_STREAM: this is a continuous stream.
    buffer.Flags = 0;

    HRESULT hr = global_audio.source_voice->SubmitSourceBuffer(&buffer);

    if (FAILED(hr)) {
        win32_log_hresult("submitting XAudio2 buffer", hr);
        return false;
    }

    return true;
}

static bool process_audio(Win32SoundOutput& sound_output, GameAudioState& audio_state) {
    if (!global_audio.source_voice || !sound_output.enabled) {
        return false;
    }

    XAUDIO2_VOICE_STATE state = {};
    global_audio.source_voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

    while (state.BuffersQueued < k_audio_queued_buffer_count) {
        const u32 buffer_idx = global_audio.next_buffer_idx;

        if (!fill_sound_buffer(sound_output, audio_state, buffer_idx)) {
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

static void render_gradient(Win32OffscreenBuffer buffer, int x_offset, int y_offset) {
    u8* row = (u8*)buffer.memory;
    for (int y = 0; y < buffer.height; y++) {
        u32* pixel = (u32*)row;
        for (int x = 0; x < buffer.width; x++) {
            /*
             * memory   : BB GG RR AA
             * register : 0xAARRGGBB
             */
            u8 blue = x + x_offset;
            u8 green = y + y_offset;
            *pixel++ = (green << 8) | blue;
        }
        row += buffer.pitch;
    }
}

static void win32_resize_dib_section(Win32OffscreenBuffer* backbuffer, int w, int h) {
    if (backbuffer->memory) {
        VirtualFree(backbuffer->memory, 0, MEM_RELEASE);
    }

    backbuffer->width = w;
    backbuffer->height = h;
    backbuffer->bitmap_info = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = backbuffer->width,
            .biHeight = backbuffer->height,
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        }
    };
    backbuffer->bytes_per_pixel = k_bytes_per_pixel;
    backbuffer->pitch = w * k_bytes_per_pixel;

    int bitmap_memory_size = w * h * k_bytes_per_pixel;
    backbuffer->memory = VirtualAlloc(0, bitmap_memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    // render_gradient(*backbuffer, 128, 0);
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
        backbuffer.memory,
        &backbuffer.bitmap_info,
        DIB_RGB_COLORS,
        SRCCOPY
    );
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
        u32 vk_code = w_param;
        bool was_down = (l_param & (1 << 30)) != 0;
        bool is_down = (l_param & (1 << 31)) == 0;
        if (is_down == was_down) {
            break;
        }

        if (vk_code == 'W') {
        }
        if (vk_code == 'A') {
        }
        if (vk_code == 'S') {
        }
        if (vk_code == 'D') {
        }
        if (vk_code == 'Q') {
        }
        if (vk_code == 'E') {
        }
        if (vk_code == VK_UP) {
        }
        if (vk_code == VK_DOWN) {
        }
        if (vk_code == VK_LEFT) {
        }
        if (vk_code == VK_RIGHT) {
        }
        if (vk_code == VK_ESCAPE) {
        }
        if (vk_code == VK_SPACE) {
        }

        bool alt_key_was_down = ((l_param >> 29) & 1) != 0;
        if (vk_code == VK_F4 && alt_key_was_down) {
            running = false;
        }

        break;
    }
    default: {
        result = DefWindowProc(window, message, w_param, l_param);
        break;
    }
    }
    return result;
}

int CALLBACK WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, LPSTR lp_cmd_line, int n_cmd_show) {
    LARGE_INTEGER perf_freq_res;
    QueryPerformanceFrequency(&perf_freq_res);
    i64 perf_freq = perf_freq_res.QuadPart;

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
            int x_offset = 0;
            int y_offset = 0;
            win32_load_xinput();

            Win32SoundOutput sound_output = {};
            GameAudioState audio_state = {};
            if (!win32_init_xaudio2(sound_output)) {
                win32_log(LogSeverity::Error, "failed to init XAudio2\n");
                running = false;
            }

            u64 last_cycle_count = __rdtsc();
            LARGE_INTEGER last_counter;
            QueryPerformanceCounter(&last_counter);

            while (running) {
                MSG message;
                while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT) {
                        running = false;
                    }
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }

                for (DWORD idx = 0; idx < XUSER_MAX_COUNT; idx++) {
                    XINPUT_STATE controller_state;
                    if (XInputGetState(idx, &controller_state) == ERROR_SUCCESS) {
                        XINPUT_GAMEPAD* pad = &controller_state.Gamepad;
                        bool up = pad->wButtons & XINPUT_GAMEPAD_DPAD_UP;
                        bool down = pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN;
                        bool left = pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT;
                        bool right = pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT;
                        bool start = pad->wButtons & XINPUT_GAMEPAD_START;
                        bool back = pad->wButtons & XINPUT_GAMEPAD_BACK;
                        bool lb = pad->wButtons & XINPUT_GAMEPAD_DPAD_UP;
                        bool rb = pad->wButtons & XINPUT_GAMEPAD_DPAD_UP;
                        bool a_btn = pad->wButtons & XINPUT_GAMEPAD_A;
                        bool b_btn = pad->wButtons & XINPUT_GAMEPAD_B;
                        bool x_btn = pad->wButtons & XINPUT_GAMEPAD_X;
                        bool y_btn = pad->wButtons & XINPUT_GAMEPAD_Y;

                        i16 stick_x = pad->sThumbLX;
                        i16 stick_y = pad->sThumbLY;

                        x_offset += stick_x / 4096;
                        y_offset += stick_y / 4096;

                        audio_state.tone_hz = 512 + (int)(256.0f * ((f32)stick_y / 30000.0f));
                    } else {
                        win32_log(LogSeverity::Warn, "cannot get controller %d state", idx);
                    }
                }

                GameOffscreenBuffer offscreen_buffer = {
                    .memory = global_backbuffer.memory,
                    .width = global_backbuffer.width,
                    .height = global_backbuffer.height,
                    .pitch = global_backbuffer.pitch,
                };
                game_render(offscreen_buffer, x_offset, y_offset);
                process_audio(sound_output, audio_state);

                HDC device_ctx = GetDC(window_handle);
                Win32WindowDim window_dim = get_window_dim(window_handle);
                win32_display_window_buffer(device_ctx, global_backbuffer, window_dim.width, window_dim.height);
                ReleaseDC(window_handle, device_ctx);

                u64 end_cycle_count = __rdtsc();
                LARGE_INTEGER end_counter;
                QueryPerformanceCounter(&end_counter);

                i64 cycles_elapsed = end_cycle_count - last_cycle_count;
                i64 counter_elapsed = end_counter.QuadPart - last_counter.QuadPart;
                f32 ms_per_frame =
                    static_cast<f32>(1000.0f * static_cast<f32>(counter_elapsed) / static_cast<f32>(perf_freq));
                f32 fps = static_cast<f32>(static_cast<f32>(perf_freq) / static_cast<f32>(counter_elapsed));
                f32 mcpf = static_cast<f32>(static_cast<f32>(cycles_elapsed) / (1000.0f * 1000.0f));

                win32_log(LogSeverity::Info, "%.02fms/f | %.02ffps | %.02fmcpf\n", ms_per_frame, fps, mcpf);

                last_counter = end_counter;
                last_cycle_count = end_cycle_count;
            }
        } else {
            // TODO logging
        }
    } else {
        // TODO logging
    }
}
