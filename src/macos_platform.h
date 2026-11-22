#pragma once

#include "platform.h"

struct MacOSWindow {
    void* native_window = nullptr;
    void* native_view = nullptr;
    bool running = false;
    int width = 0;
    int height = 0;
};

bool macos_window_init(MacOSWindow* window, int width, int height, const char* title);
// Drains the event queue; keyboard events are recorded into `kb`.
void macos_window_pump_events(MacOSWindow* window, hh::ControllerInput& kb);
void macos_window_present(MacOSWindow* window, const hh::ScreenBuffer& buffer);
void macos_window_shutdown(MacOSWindow* window);
// Fills gamepad controllers (slots 1..k_max_controllers-1).
void macos_poll_gamepads(hh::Input& curr_input, const hh::Input& prev_input);
