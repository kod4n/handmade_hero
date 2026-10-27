#include <math.h>
#include <stdint.h>
#include <windows.h>
#include <xinput.h>
#include <dsound.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;

struct Win32OffscreenBuffer {
    BITMAPINFO bitmap_info;
    void* memory;
    int width;
    int height;
    int bytes_per_pixel;
    int pitch;
};

struct Win32SoundOutput {
    u32 samples_per_second;
    u32 tone_hz;
    i16 tone_volume;
    u32 running_sample_idx;
    u32 wave_period;
    int bytes_per_sample;
    u32 buffer_size;
    f32 t_sine;
    u32 latency_sample_count;
};

static bool running;
static Win32OffscreenBuffer global_backbuffer;
static LPDIRECTSOUNDBUFFER global_sound_buffer;

// constants
const int k_bytes_per_pixel = 4;
const f32 k_pi = 3.141592653459f;

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

#define DIRECT_SOUND_CREATE(name) \
    HRESULT WINAPI name(LPGUID pc_guid_device, LPDIRECTSOUND* pp_ds, LPUNKNOWN p_unk_outer)
typedef DIRECT_SOUND_CREATE(_DirectSoundCreate);

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

static void win32_init_dsound(HWND window, u32 samples_per_second, u32 buffer_size) {
    HMODULE lib = LoadLibrary("dsound.dll");
    if (lib) {
        _DirectSoundCreate* direct_sound_create = (_DirectSoundCreate*)GetProcAddress(lib, "DirectSoundCreate");
        LPDIRECTSOUND direct_sound;
        if (!direct_sound_create || !SUCCEEDED(direct_sound_create(0, &direct_sound, 0))) {
            OutputDebugString("[warn] error creating direct sound handle");
            return;
        }
        if (!SUCCEEDED(direct_sound->SetCooperativeLevel(window, DSSCL_PRIORITY))) {
            OutputDebugString("[warn] error setting dsound coop level");
            return;
        }

        DSBUFFERDESC buffer_desc = {
            .dwSize = sizeof(buffer_desc),
            .dwFlags = DSBCAPS_PRIMARYBUFFER,
        };
        LPDIRECTSOUNDBUFFER sound_buffer;
        if (!SUCCEEDED(direct_sound->CreateSoundBuffer(&buffer_desc, &sound_buffer, 0))) {
            OutputDebugString("[warn] error setting dsound buffer");
            return;
        }

        WAVEFORMATEX wave_fmt = {
            .wFormatTag = WAVE_FORMAT_PCM,
            .nChannels = 2,
            .nSamplesPerSec = samples_per_second,
            .wBitsPerSample = 16,
        };
        wave_fmt.nBlockAlign = wave_fmt.nChannels * wave_fmt.wBitsPerSample / 8;
        wave_fmt.nAvgBytesPerSec = wave_fmt.nSamplesPerSec * wave_fmt.nBlockAlign;
        if (!SUCCEEDED(sound_buffer->SetFormat(&wave_fmt))) {
            OutputDebugString("[warn] error setting dsound format");
            return;
        }

        DSBUFFERDESC sec_buffer_desc = {
            .dwSize = sizeof(sec_buffer_desc),
            .dwFlags = 0,
            .dwBufferBytes = buffer_size,
            .lpwfxFormat = &wave_fmt,
        };
        if (!SUCCEEDED(direct_sound->CreateSoundBuffer(&sec_buffer_desc, &global_sound_buffer, 0))) {
            OutputDebugString("[warn] error creating secondary dsound buffer");
            return;
        }
    }
}

static void update_sound_buffer_region(Win32SoundOutput& sound_output, void* region, DWORD region_size) {
    i16* sample_out = (i16*)region;
    DWORD region_sample_count = region_size / sound_output.bytes_per_sample;
    for (DWORD idx = 0; idx < region_sample_count; idx++) {
        f32 sine = sinf(sound_output.t_sine);
        i16 sample = (i16)(sine * sound_output.tone_volume);
        *sample_out++ = sample;
        *sample_out++ = sample;
        sound_output.t_sine += 2.0f * k_pi * 1.0f / (f32)sound_output.wave_period;
        ++sound_output.running_sample_idx;
    }
}

static void fill_sound_buffer(Win32SoundOutput& sound_output, DWORD offset, DWORD bytes) {
    void* region1 = nullptr;
    DWORD region1_size;
    void* region2 = nullptr;
    DWORD region2_size;

    HRESULT error = global_sound_buffer->Lock(offset, bytes, &region1, &region1_size, &region2, &region2_size, 0);
    if (!SUCCEEDED(error)) {
        OutputDebugString("[warn] error locking sound buffer\n");
        return;
    }
    OutputDebugString("[success] locking sound buffer\n");

    update_sound_buffer_region(sound_output, region1, region1_size);
    update_sound_buffer_region(sound_output, region2, region2_size);

    global_sound_buffer->Unlock(region1, region1_size, region2, region2_size);
}

static void process_audio(Win32SoundOutput& sound_output) {
    DWORD play_cursor;
    DWORD write_cursor;
    if (!SUCCEEDED(global_sound_buffer->GetCurrentPosition(&play_cursor, &write_cursor))) {
        OutputDebugString("[warn] error getting sound buffer current position\n");
        return;
    }

    DWORD bytes;
    DWORD offset = (sound_output.running_sample_idx * sound_output.bytes_per_sample) % sound_output.buffer_size;
    DWORD target_cursor =
        (play_cursor + (sound_output.latency_sample_count * sound_output.bytes_per_sample)) % sound_output.buffer_size;
    if (offset > target_cursor) {
        bytes = sound_output.buffer_size - offset;
        bytes += target_cursor;
    } else {
        bytes = target_cursor - offset;
    }

    fill_sound_buffer(sound_output, offset, bytes);
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
    render_gradient(*backbuffer, 128, 0);
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

            Win32SoundOutput sound_output = {
                .samples_per_second = 48000,
                .tone_hz = 256,
                .tone_volume = 500,
                .running_sample_idx = 0,
                .wave_period = sound_output.samples_per_second / sound_output.tone_hz,
                .bytes_per_sample = sizeof(i16) * 2,
                .buffer_size = sound_output.samples_per_second * sound_output.bytes_per_sample,
                .latency_sample_count = sound_output.samples_per_second / 10,
            };
            win32_init_dsound(window_handle, sound_output.samples_per_second, sound_output.buffer_size);
            fill_sound_buffer(sound_output, 0, sound_output.latency_sample_count * sound_output.bytes_per_sample);
            bool sound_is_playing = false;

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

                        sound_output.tone_hz = 512 + (int)(256.0f * ((f32)stick_y / 30000.0f));
                        sound_output.wave_period = sound_output.samples_per_second / sound_output.tone_hz;
                    } else {
                    }
                }

                render_gradient(global_backbuffer, x_offset, y_offset);
                process_audio(sound_output);

                if (!sound_is_playing) {
                    global_sound_buffer->Play(0, 0, DSBPLAY_LOOPING);
                    sound_is_playing = true;
                }

                HDC device_ctx = GetDC(window_handle);
                Win32WindowDim window_dim = get_window_dim(window_handle);
                win32_display_window_buffer(device_ctx, global_backbuffer, window_dim.width, window_dim.height);
                ReleaseDC(window_handle, device_ctx);
            }
        } else {
            // TODO logging
        }
    } else {
        // TODO logging
    }
}
