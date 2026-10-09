#include <stdint.h>
#include <windows.h>

static bool running;

// bitmaps
static BITMAPINFO bitmap_info;
static void* bitmap_memory;
static int bitmap_width;
static int bitmap_height;
static int bytes_per_pixel = 4;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

static void render_gradient(int x_offset, int y_offset) {
    int w = bitmap_width;
    int h = bitmap_height;
    int pitch = w * bytes_per_pixel;
    u8* row = (u8*)bitmap_memory;
    for (int y = 0; y < bitmap_height; y++) {
        u32* pixel = (u32*)row;
        for (int x = 0; x < bitmap_width; x++) {
            /*
             * memory   : BB GG RR AA
             * register : 0xAARRGGBB
             */
            u8 blue = x + x_offset;
            u8 green = y + y_offset;
            *pixel++ = (green << 8) | blue;
        }
        row += pitch;
    }
}

static void win32_resize_dib_section(int w, int h) {
    if (bitmap_memory) {
        VirtualFree(bitmap_memory, 0, MEM_RELEASE);
    }

    bitmap_width = w;
    bitmap_height = h;

    bitmap_info = {
        .bmiHeader = {
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = bitmap_width,
            .biHeight = bitmap_height,
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        }
    };

    int bitmap_memory_size = (bitmap_width * bitmap_height) * bytes_per_pixel;
    bitmap_memory = VirtualAlloc(0, bitmap_memory_size, MEM_COMMIT, PAGE_READWRITE);
    render_gradient(128, 0);
}

static void win32_update_window(HDC device_ctx, RECT* window_rect, int x, int y, int w, int h) {
    int window_width = window_rect->right - window_rect->left;
    int window_height = window_rect->bottom - window_rect->top;
    StretchDIBits(
        device_ctx,
        0,
        0,
        bitmap_width,
        bitmap_height,
        0,
        0,
        bitmap_width,
        bitmap_height,
        bitmap_memory,
        &bitmap_info,
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
    case WM_SIZE: {
        OutputDebugStringA("WM_SIZE\n");
        RECT client_rect;
        GetWindowRect(window, &client_rect);
        int width = client_rect.right - client_rect.left;
        int height = client_rect.bottom - client_rect.top;
        win32_resize_dib_section(width, height);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC device_ctx = BeginPaint(window, &paint);
        int x = paint.rcPaint.left;
        int y = paint.rcPaint.top;
        int height = paint.rcPaint.bottom - paint.rcPaint.top;
        int width = paint.rcPaint.right - paint.rcPaint.left;
        RECT client_rect;
        GetClientRect(window, &client_rect);
        win32_update_window(device_ctx, &client_rect, x, y, width, height);
        EndPaint(window, &paint);
        OutputDebugStringA("WM_PAINT\n");
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
    default: {
        result = DefWindowProc(window, message, w_param, l_param);
        break;
    }
    }
    return result;
}

int CALLBACK WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, LPSTR lp_cmd_line, int n_cmd_show) {
    WNDCLASS window_class = {
        .style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = window_callback,
        .hInstance = h_instance,
        .lpszClassName = "HandmadeHeroWindowClass",
    };
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
                render_gradient(x_offset, y_offset);

                HDC device_ctx = GetDC(window_handle);
                RECT client_rect;
                GetWindowRect(window_handle, &client_rect);
                int window_width = client_rect.right - client_rect.left;
                int window_height = client_rect.bottom - client_rect.top;
                win32_update_window(device_ctx, &client_rect, 0, 0, window_width, window_height);
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
