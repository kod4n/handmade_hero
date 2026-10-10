#include <stdint.h>
#include <windows.h>

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

// constants
const int k_bytes_per_pixel = 4;

struct Win32WindowDim {
    int width;
    int height;
};

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
    backbuffer->memory = VirtualAlloc(0, bitmap_memory_size, MEM_COMMIT, PAGE_READWRITE);
    render_gradient(*backbuffer, 128, 0);
}

static void
win32_display_window_buffer(HDC device_ctx, Win32OffscreenBuffer backbuffer, int window_width, int window_height) {
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
    // case WM_SIZE: {
    //     OutputDebugStringA("WM_SIZE\n");
    //     Win32WindowDim window_dim = get_window_dim(window);
    //     win32_resize_dib_section(&global_backbuffer, window_dim.width, window_dim.height);
    //     break;
    // }
    // case WM_PAINT: {
    //     PAINTSTRUCT paint;
    //     HDC device_ctx = BeginPaint(window, &paint);
    //     Win32WindowDim window_dim = get_window_dim(window);
    //     win32_display_window_buffer(device_ctx, global_backbuffer, window_dim.width, window_dim.height);
    //     EndPaint(window, &paint);
    //     OutputDebugStringA("WM_PAINT\n");
    //     break;
    // }
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
            while (running) {
                MSG message;
                while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT) {
                        running = false;
                    }
                    TranslateMessage(&message);
                    DispatchMessage(&message);
                }
                render_gradient(global_backbuffer, x_offset, y_offset);

                HDC device_ctx = GetDC(window_handle);
                Win32WindowDim window_dim = get_window_dim(window_handle);
                win32_display_window_buffer(device_ctx, global_backbuffer, window_dim.width, window_dim.height);
                ReleaseDC(window_handle, device_ctx);
                x_offset++;
            }
        } else {
            // TODO logging
        }
    } else {
        // TODO logging
    }
}
