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

struct Win32OffscreenBuffer {
    BITMAPINFO bitmap_info;
    void* memory;
    int width;
    int height;
    int bytes_per_pixel;
    int pitch;
};

static bool running;
static Win32OffscreenBuffer global_backbuffer;
static LPDIRECTSOUNDBUFFER global_sound_buffer;

// constants
const int k_bytes_per_pixel = 4;

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
            .cbSize = 0,
        };
        wave_fmt.nBlockAlign = (u16)((wave_fmt.nChannels * wave_fmt.wBitsPerSample) / 8);
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

static void fill_sound_buffer(
    i16 volume,
    int bytes_per_sample,
    u32& running_sample_idx,
    int half_square_wave_period,
    int buffer_size
) {
    DWORD play_cursor;
    DWORD write_cursor;
    int error = global_sound_buffer->GetCurrentPosition(&play_cursor, &write_cursor);
    if (!SUCCEEDED(error)) {
        OutputDebugString("[warn] error getting sound buffer current position\n");
        return;
    }

    DWORD byte_to_lock = running_sample_idx * bytes_per_sample % buffer_size;
    DWORD bytes_to_write;
    if (byte_to_lock == play_cursor) {
        bytes_to_write = buffer_size;
    } else if (byte_to_lock > play_cursor) {
        bytes_to_write = buffer_size - byte_to_lock;
        bytes_to_write += play_cursor;
    } else {
        bytes_to_write = play_cursor - byte_to_lock;
    }

    void* region1 = nullptr;
    DWORD region1_size;
    void* region2 = nullptr;
    DWORD region2_size;
    HRESULT _err =
        global_sound_buffer->Lock(byte_to_lock, bytes_to_write, &region1, &region1_size, &region2, &region2_size, 0);
    if (!SUCCEEDED(_err)) {
        int code = HRESULT_CODE(_err);
        OutputDebugString("[warn] error locking sound buffer\n");
        return;
    }

    i16* sample_out = (i16*)region1;
    DWORD region1_sample_count = region1_size / bytes_per_sample;
    for (DWORD idx = 0; idx < region1_sample_count; idx++) {
        i16 sample = ((running_sample_idx++ / half_square_wave_period) % 2) ? volume : -volume;
        *sample_out++ = sample;
        *sample_out++ = sample;
    }

    sample_out = (i16*)region2;
    DWORD region2_sample_count = region2_size / bytes_per_sample;
    for (DWORD idx = 0; idx < region2_sample_count; idx++) {
        i16 sample = ((running_sample_idx++ / half_square_wave_period) % 2) ? volume : -volume;
        *sample_out++ = sample;
        *sample_out++ = sample;
    }

    global_sound_buffer->Unlock(region1, region1_size, region2, region2_size);
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

            int tone_hz = 256;
            i16 tone_volume = 200;
            int samples_per_second = 48000;
            u32 running_sample_idx = 0;
            int square_wave_period = samples_per_second / tone_hz;
            int half_square_wave_period = square_wave_period / 2;
            int bytes_per_sample = sizeof(i16) * 2;
            int buffer_size = samples_per_second * bytes_per_sample;
            win32_init_dsound(window_handle, samples_per_second, buffer_size);
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

                        x_offset += stick_x >> 12;
                        y_offset += stick_y >> 12;
                    } else {
                    }
                }

                render_gradient(global_backbuffer, x_offset, y_offset);
                fill_sound_buffer(
                    tone_volume,
                    bytes_per_sample,
                    running_sample_idx,
                    half_square_wave_period,
                    buffer_size
                );

                if (!sound_is_playing) {
                    global_sound_buffer->Play(0, 0, DSBPLAY_LOOPING);
                    sound_is_playing = !sound_is_playing;
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
