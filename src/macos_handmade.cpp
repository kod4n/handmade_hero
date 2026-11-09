#include "game.cpp"
#include "defer.h"
#include "macos_platform.h"

#include <AudioToolbox/AudioToolbox.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <utility>

using namespace HH;

// render constants
static constexpr u32 k_monitor_refresh_hz = 60;
static constexpr u32 k_game_refresh_hz = k_monitor_refresh_hz;
static constexpr u32 k_initial_window_width = 1280;
static constexpr u32 k_initial_window_height = 720;

// audio constants
static constexpr u32 k_audio_channels = 2;
static constexpr u32 k_audio_bits_per_channel_sample = 16;
static constexpr u32 k_audio_bytes_per_frame = sizeof(i16) * k_audio_channels;
static_assert(sizeof(AudioFrame) == k_audio_bytes_per_frame);
static constexpr u32 k_sound_samples_per_second = 48000;
static constexpr u32 k_audio_frames_per_buffer = 480;
static constexpr u32 k_audio_buffer_size_bytes = k_audio_frames_per_buffer * k_audio_bytes_per_frame;
static constexpr u32 k_audio_buffer_count = 3;

struct MacOSOffscreenBuffer {
    std::span<u32> pixels;
    u32 width = 0;
    u32 height = 0;
};

struct MacOSAudio {
    AudioQueueRef queue = nullptr;
    std::array<AudioQueueBufferRef, k_audio_buffer_count> buffers = {};
    // Shared between the game (main thread) and the AudioQueue callback thread.
    AudioState state = {};
};

enum class LogSeverity { Info, Warn, Error };

static MacOSOffscreenBuffer global_backbuffer;
static MacOSAudio global_audio;

static const char* log_severity(LogSeverity severity) {
    switch (severity) {
    case LogSeverity::Info:
        return "info";
    case LogSeverity::Warn:
        return "warn";
    case LogSeverity::Error:
        return "error";
    }
    return "unknown";
}

static void macos_log(LogSeverity severity, const char* format, ...) {
    char buffer[1024] = {};
    int prefix_length = snprintf(buffer, sizeof(buffer), "[%s] ", log_severity(severity));
    if (prefix_length < 0 || static_cast<size_t>(prefix_length) >= sizeof(buffer)) {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(buffer + prefix_length, sizeof(buffer) - static_cast<size_t>(prefix_length), format, args);
    va_end(args);

    fputs(buffer, stderr);
}

static void* macos_alloc(u64 size, void* base_address = nullptr) {
    void* memory = mmap(base_address, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return memory == MAP_FAILED ? nullptr : memory;
}

static void macos_free(void* memory, u64 size) {
    if (memory) {
        munmap(memory, size);
    }
}

#if DEV
// mmap needs the size to unmap; stash it in a header page before the content.
static constexpr u64 k_dev_file_header_size = 16;

namespace HH {
ReadFileResult dev_platform_read_file(const char* filename) {
    macos_log(LogSeverity::Info, "attempt file read for %s\n", filename);
    ReadFileResult result = {};
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        macos_log(LogSeverity::Warn, "failed to read file %s\n", filename);
        return result;
    }
    defer(close(fd));

    struct stat file_stat;
    if (fstat(fd, &file_stat) != 0) {
        return result;
    }

    u32 file_size = safe_truncate(static_cast<u64>(file_stat.st_size));
    u8* block = static_cast<u8*>(macos_alloc(file_size + k_dev_file_header_size));
    if (!block) {
        return result;
    }
    *reinterpret_cast<u64*>(block) = file_size + k_dev_file_header_size;
    result.content = block + k_dev_file_header_size;

    ssize_t bytes_read = read(fd, result.content, file_size);
    if (bytes_read == static_cast<ssize_t>(file_size)) {
        result.size = file_size;
    } else {
        dev_platform_free_file(result.content);
        result.content = nullptr;
    }
    return result;
}

void dev_platform_free_file(void* memory) {
    if (memory) {
        u8* block = static_cast<u8*>(memory) - k_dev_file_header_size;
        macos_free(block, *reinterpret_cast<u64*>(block));
    }
}

bool dev_platform_write_file(const char* filename, u32 size, void* memory) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        macos_log(LogSeverity::Warn, "failed to write file %s\n", filename);
        return false;
    }
    defer(close(fd));

    ssize_t bytes_written = write(fd, memory, size);
    return bytes_written == static_cast<ssize_t>(size);
}
}  // namespace HH
#endif

static void macos_resize_backbuffer(MacOSOffscreenBuffer& backbuffer, u32 width, u32 height) {
    if (width == 0 || height == 0) {
        return;
    }

    if (!backbuffer.pixels.empty()) {
        macos_free(backbuffer.pixels.data(), backbuffer.pixels.size_bytes());
        backbuffer.pixels = {};
    }

    u64 pixel_count = static_cast<u64>(width) * height;
    u32* memory = static_cast<u32*>(macos_alloc(pixel_count * sizeof(u32)));
    if (!memory) {
        macos_log(LogSeverity::Error, "mmap failed while allocating backbuffer\n");
        return;
    }

    backbuffer.pixels = {memory, pixel_count};
    backbuffer.width = width;
    backbuffer.height = height;
}

// Fills one AudioQueue buffer from the game and queues it for playback.
static bool macos_fill_audio_buffer(AudioQueueRef queue, AudioQueueBufferRef buffer, AudioState& state) {
    HH::AudioBuffer game_buffer = {
        .frames =
            {static_cast<AudioFrame*>(buffer->mAudioData), buffer->mAudioDataBytesCapacity / k_audio_bytes_per_frame},
        .samples_per_second = k_sound_samples_per_second,
    };
    game_sound(game_buffer, state);
    buffer->mAudioDataByteSize = static_cast<u32>(game_buffer.frames.size_bytes());

    OSStatus status = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    if (status != noErr) {
        macos_log(LogSeverity::Error, "AudioQueueEnqueueBuffer failed: %d\n", static_cast<int>(status));
        return false;
    }
    return true;
}

static void macos_audio_callback(void* user_data, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    MacOSAudio* audio = static_cast<MacOSAudio*>(user_data);
    if (audio && audio->queue) {
        macos_fill_audio_buffer(queue, buffer, audio->state);
    }
}

static bool macos_init_audio() {
    AudioStreamBasicDescription format = {
        .mSampleRate = k_sound_samples_per_second,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = k_audio_bytes_per_frame,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = k_audio_bytes_per_frame,
        .mChannelsPerFrame = k_audio_channels,
        .mBitsPerChannel = k_audio_bits_per_channel_sample,
    };

    OSStatus status =
        AudioQueueNewOutput(&format, macos_audio_callback, &global_audio, nullptr, nullptr, 0, &global_audio.queue);
    if (status != noErr) {
        macos_log(LogSeverity::Error, "AudioQueueNewOutput failed: %d\n", static_cast<int>(status));
        global_audio.queue = nullptr;
        return false;
    }

    // Prime every buffer before starting playback to avoid an initial underrun.
    for (AudioQueueBufferRef& buffer : global_audio.buffers) {
        status = AudioQueueAllocateBuffer(global_audio.queue, k_audio_buffer_size_bytes, &buffer);
        if (status != noErr) {
            macos_log(LogSeverity::Error, "AudioQueueAllocateBuffer failed: %d\n", static_cast<int>(status));
            return false;
        }
        if (!macos_fill_audio_buffer(global_audio.queue, buffer, global_audio.state)) {
            return false;
        }
    }

    status = AudioQueueStart(global_audio.queue, nullptr);
    if (status != noErr) {
        macos_log(LogSeverity::Error, "AudioQueueStart failed: %d\n", static_cast<int>(status));
        return false;
    }
    return true;
}

static void macos_shutdown_audio() {
    if (global_audio.queue) {
        AudioQueueRef queue = global_audio.queue;
        global_audio.queue = nullptr;
        AudioQueueStop(queue, true);
        AudioQueueDispose(queue, true);
    }
    global_audio.buffers = {};
}

static u64 macos_wall_clock() {
    return mach_absolute_time();
}

static f32 macos_seconds_elapsed(u64 start, u64 end) {
    static mach_timebase_info_data_t timebase = {};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    double nanoseconds = static_cast<double>(end - start) * timebase.numer / timebase.denom;
    return static_cast<f32>(nanoseconds / 1'000'000'000.0);
}

int main() {
    MacOSWindow window = {};
    if (!macos_window_init(&window, k_initial_window_width, k_initial_window_height, "Handmade Hero")) {
        macos_log(LogSeverity::Error, "failed to create macOS window\n");
        return 1;
    }
    defer(macos_window_shutdown(&window));

    macos_resize_backbuffer(global_backbuffer, k_initial_window_width, k_initial_window_height);
    if (global_backbuffer.pixels.empty()) {
        return 1;
    }
    defer(macos_free(global_backbuffer.pixels.data(), global_backbuffer.pixels.size_bytes()));

    if (!macos_init_audio()) {
        macos_log(LogSeverity::Warn, "audio initialization failed; continuing without audio\n");
    }
    defer(macos_shutdown_audio());

#if DEV
    void* base_address = reinterpret_cast<void*>(2_TB);
#else
    void* base_address = nullptr;
#endif

    Storage storage = {
        .main_size = 64_MB,
        .transient_size = 4_GB,
    };
    u64 total_storage_size = storage.main_size + storage.transient_size;
    storage.main = macos_alloc(total_storage_size, base_address);
    if (!storage.main) {
        macos_log(LogSeverity::Error, "failed to allocate main game storage\n");
        return 1;
    }
    defer(macos_free(storage.main, total_storage_size));
    storage.transient = static_cast<u8*>(storage.main) + storage.main_size;
#if DEV
    storage.dev_read_file = dev_platform_read_file;
    storage.dev_free_file = dev_platform_free_file;
    storage.dev_write_file = dev_platform_write_file;
#endif

    const f32 target_seconds_per_frame = 1.0f / static_cast<f32>(k_game_refresh_hz);
    u64 last_counter = macos_wall_clock();

    Input input[2] = {};
    Input* curr_input = &input[0];
    Input* prev_input = &input[1];

    while (window.running) {
        ControllerInput* kb = &curr_input->controllers[0];
        ControllerInput* prev_kb = &prev_input->controllers[0];
        *kb = {};
        kb->is_connected = true;
        for (u8 i = 0; i < std::size(kb->actions); i++) {
            kb->actions[i].ended_down = prev_kb->actions[i].ended_down;
        }
        macos_window_pump_events(&window, *kb);
        if (!window.running) {
            break;
        }
        macos_poll_gamepads(*curr_input, *prev_input);

        ScreenBuffer offscreen_buffer = {
            .pixels = global_backbuffer.pixels,
            .width = global_backbuffer.width,
            .height = global_backbuffer.height,
            .pitch = global_backbuffer.width,
        };
        game_render(storage, offscreen_buffer, *curr_input, global_audio.state);

        macos_window_present(&window, offscreen_buffer);

        f32 frame_seconds = macos_seconds_elapsed(last_counter, macos_wall_clock());
        if (frame_seconds < target_seconds_per_frame) {
            while (frame_seconds < target_seconds_per_frame) {
                auto sleep_us = static_cast<useconds_t>(1'000'000.0f * (target_seconds_per_frame - frame_seconds));
                if (sleep_us > 1000) {
                    // Leave ~1ms to spin so oversleeping doesn't miss the frame.
                    usleep(sleep_us - 1000);
                }
                frame_seconds = macos_seconds_elapsed(last_counter, macos_wall_clock());
            }
        } else {
            macos_log(
                LogSeverity::Warn,
                "frame time [%.04f] missed target frame time [%.04f]\n",
                frame_seconds,
                target_seconds_per_frame
            );
        }

        u64 end_counter = macos_wall_clock();
        f32 seconds_per_frame = macos_seconds_elapsed(last_counter, end_counter);
        last_counter = end_counter;

        macos_log(LogSeverity::Info, "%.02fms/f | %.02ffps\n", 1000.0f * seconds_per_frame, 1.0f / seconds_per_frame);

        std::swap(curr_input, prev_input);
    }

    return 0;
}
