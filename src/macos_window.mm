#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <GameController/GameController.h>

#include "macos_platform.h"

using namespace HH;

// macOS virtual key codes (kVK_ANSI_* in <Carbon/HIToolbox/Events.h>).
enum MacOSKeyCode : u16 {
  k_key_a = 0x00,
  k_key_s = 0x01,
  k_key_d = 0x02,
  k_key_q = 0x0C,
  k_key_w = 0x0D,
  k_key_e = 0x0E,
};

// Keyboard controller being filled during macos_window_pump_events.
static ControllerInput *global_kb = nullptr;

static void macos_process_kb_button(ActionState &state, bool is_down) {
  state.ended_down = is_down;
  state.half_transition_count++;
}

static void macos_process_key(u16 key_code, bool is_down) {
  if (!global_kb) {
    return;
  }
  ControllerInput &kb = *global_kb;
  switch (key_code) {
  case k_key_w:
    macos_process_kb_button(input_action(kb, Action::MoveUp), is_down);
    break;
  case k_key_a:
    macos_process_kb_button(input_action(kb, Action::MoveLeft), is_down);
    break;
  case k_key_s:
    macos_process_kb_button(input_action(kb, Action::MoveDown), is_down);
    break;
  case k_key_d:
    macos_process_kb_button(input_action(kb, Action::MoveRight), is_down);
    break;
  case k_key_q:
    macos_process_kb_button(input_action(kb, Action::LeftShoulder), is_down);
    break;
  case k_key_e:
    macos_process_kb_button(input_action(kb, Action::RightShoulder), is_down);
    break;
  }
}

static void macos_process_pad_button(bool is_down, const ActionState &old_state,
                                     ActionState &new_state) {
  new_state.ended_down = is_down;
  new_state.half_transition_count =
      (old_state.ended_down != new_state.ended_down) ? 1 : 0;
}

static f32 macos_normalize_stick(f32 value, f32 dead_zone) {
  return (value > -dead_zone && value < dead_zone) ? 0.0f : value;
}

void macos_poll_gamepads(Input &curr_input, const Input &prev_input) {
  NSArray<GCController *> *controllers = GCController.controllers;
  NSUInteger pad_count = controllers.count;

  // Slot 0 is the keyboard.
  for (u32 idx = 0; idx < k_max_controllers - 1; idx++) {
    ControllerInput &new_input = curr_input.controllers[idx + 1];
    const ControllerInput &old_input = prev_input.controllers[idx + 1];

    GCExtendedGamepad *pad =
        idx < pad_count ? controllers[idx].extendedGamepad : nil;
    if (!pad) {
      new_input.is_connected = false;
      continue;
    }

    new_input.is_analog = true;
    new_input.is_connected = true;

    // stick (GameController already reports -1..1, +y up)
    constexpr f32 dead_zone = 0.24f;
    new_input.stick_avg_x =
        macos_normalize_stick(pad.leftThumbstick.xAxis.value, dead_zone);
    new_input.stick_avg_y =
        macos_normalize_stick(pad.leftThumbstick.yAxis.value, dead_zone);

    // dpad, overrides joystick when used
    if (pad.dpad.up.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg_y = 1.0f;
    }
    if (pad.dpad.down.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg_y = -1.0f;
    }
    if (pad.dpad.right.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg_x = 1.0f;
    }
    if (pad.dpad.left.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg_x = -1.0f;
    }

    constexpr f32 move_threshold = 0.5f;
    macos_process_pad_button(new_input.stick_avg_x < -move_threshold,
                             input_action(old_input, Action::MoveLeft),
                             input_action(new_input, Action::MoveLeft));
    macos_process_pad_button(new_input.stick_avg_x > move_threshold,
                             input_action(old_input, Action::MoveRight),
                             input_action(new_input, Action::MoveRight));
    macos_process_pad_button(new_input.stick_avg_y < -move_threshold,
                             input_action(old_input, Action::MoveDown),
                             input_action(new_input, Action::MoveDown));
    macos_process_pad_button(new_input.stick_avg_y > move_threshold,
                             input_action(old_input, Action::MoveUp),
                             input_action(new_input, Action::MoveUp));

    // buttons
    macos_process_pad_button(pad.buttonY.isPressed,
                             input_action(old_input, Action::ActionUp),
                             input_action(new_input, Action::ActionUp));
    macos_process_pad_button(pad.buttonA.isPressed,
                             input_action(old_input, Action::ActionDown),
                             input_action(new_input, Action::ActionDown));
    macos_process_pad_button(pad.buttonX.isPressed,
                             input_action(old_input, Action::ActionLeft),
                             input_action(new_input, Action::ActionLeft));
    macos_process_pad_button(pad.buttonB.isPressed,
                             input_action(old_input, Action::ActionRight),
                             input_action(new_input, Action::ActionRight));
    macos_process_pad_button(pad.leftShoulder.isPressed,
                             input_action(old_input, Action::LeftShoulder),
                             input_action(new_input, Action::LeftShoulder));
    macos_process_pad_button(pad.rightShoulder.isPressed,
                             input_action(old_input, Action::RightShoulder),
                             input_action(new_input, Action::RightShoulder));
  }
}

@interface HandmadeView : NSView {
@public
  const void *pixels;
  size_t pitch;
  size_t pixel_width;
  size_t pixel_height;
}
@end

@implementation HandmadeView

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)drawRect:(NSRect)dirty_rect {
  (void)dirty_rect;

  if (!pixels || pixel_width == 0 || pixel_height == 0) {
    [[NSColor blackColor] setFill];
    NSRectFill(self.bounds);
    return;
  }

  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  if (!color_space) {
    return;
  }

  /*
   * The game buffer stores 0xAARRGGBB pixels. On little-endian macOS
   * this is byte ordered B, G, R, A, matching 32-bit little-endian
   * CoreGraphics bitmap storage.
   */
  CGBitmapInfo bitmap_info =
      static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little) |
      static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst);

  CGDataProviderRef provider = CGDataProviderCreateWithData(
      nullptr, pixels, pitch * pixel_height, nullptr);

  if (provider) {
    CGImageRef image = CGImageCreate(pixel_width, pixel_height, 8, 32, pitch,
                                     color_space, bitmap_info, provider,
                                     nullptr, false, kCGRenderingIntentDefault);

    if (image) {
      CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
      CGContextSaveGState(context);
      CGContextSetInterpolationQuality(context, kCGInterpolationNone);
      CGContextTranslateCTM(context, 0, NSHeight(self.bounds));
      CGContextScaleCTM(context, 1.0, -1.0);
      CGContextDrawImage(context, self.bounds, image);
      CGContextRestoreGState(context);
      CGImageRelease(image);
    }

    CGDataProviderRelease(provider);
  }

  CGColorSpaceRelease(color_space);
}

- (void)keyDown:(NSEvent *)event {
  if (!event.isARepeat) {
    macos_process_key(event.keyCode, true);
  }
}

- (void)keyUp:(NSEvent *)event {
  macos_process_key(event.keyCode, false);
}

- (void)mouseDown:(NSEvent *)event {
  (void)event;
}

- (void)mouseUp:(NSEvent *)event {
  (void)event;
}

- (void)mouseMoved:(NSEvent *)event {
  (void)event;
}

@end

@interface HandmadeWindowDelegate : NSObject <NSWindowDelegate> {
@public
  MacOSWindow *platform_window;
}
@end

@implementation HandmadeWindowDelegate

- (BOOL)windowShouldClose:(id)sender {
  (void)sender;
  if (platform_window) {
    platform_window->running = false;
  }
  return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  if (platform_window) {
    platform_window->running = false;
  }
}

@end

static HandmadeWindowDelegate *global_window_delegate;

bool macos_window_init(MacOSWindow *window, int width, int height,
                       const char *title) {
  if (!window) {
    return false;
  }

  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSRect content_rect = NSMakeRect(0, 0, width, height);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable |
                       NSWindowStyleMaskResizable;

    NSWindow *native_window =
        [[NSWindow alloc] initWithContentRect:content_rect
                                    styleMask:style
                                      backing:NSBackingStoreBuffered
                                        defer:NO];

    if (!native_window) {
      return false;
    }

    NSString *window_title =
        title ? [NSString stringWithUTF8String:title] : @"Handmade Hero";
    [native_window setTitle:window_title ?: @"Handmade Hero"];
    // ARC owns the window; don't let -close release it a second time.
    [native_window setReleasedWhenClosed:NO];
    [native_window center];
    [native_window setAcceptsMouseMovedEvents:YES];

    HandmadeView *view = [[HandmadeView alloc] initWithFrame:content_rect];
    [view setWantsLayer:YES];
    [native_window setContentView:view];
    [native_window makeFirstResponder:view];

    global_window_delegate = [[HandmadeWindowDelegate alloc] init];
    global_window_delegate->platform_window = window;
    [native_window setDelegate:global_window_delegate];

    [native_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    [native_window makeFirstResponder:view];

    window->native_window = (__bridge_retained void *)native_window;
    window->native_view = (__bridge_retained void *)view;
    window->running = true;
    window->width = width;
    window->height = height;
  }

  return true;
}

void macos_window_pump_events(MacOSWindow *window, ControllerInput &kb) {
  if (!window || !window->running) {
    return;
  }

  global_kb = &kb;
  @autoreleasepool {
    NSEvent *event = nil;
    while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES])) {
      [NSApp sendEvent:event];
    }

    [NSApp updateWindows];
  }
  global_kb = nullptr;
}

void macos_window_present(MacOSWindow *window, const ScreenBuffer &buffer) {
  if (!window || !window->native_view) {
    return;
  }

  @autoreleasepool {
    HandmadeView *view = (__bridge HandmadeView *)window->native_view;
    view->pixels = buffer.pixels.data();
    view->pitch = static_cast<size_t>(buffer.pitch) * sizeof(u32);
    view->pixel_width = static_cast<size_t>(buffer.width);
    view->pixel_height = static_cast<size_t>(buffer.height);

    [view setNeedsDisplay:YES];
    NSWindow *native_window = (__bridge NSWindow *)window->native_window;
    [native_window displayIfNeeded];
  }
}

void macos_window_shutdown(MacOSWindow *window) {
  if (!window) {
    return;
  }

  @autoreleasepool {
    if (window->native_window) {
      NSWindow *native_window =
          (__bridge_transfer NSWindow *)window->native_window;
      [native_window setDelegate:nil];
      [native_window close];
    }

    if (window->native_view) {
      (void)(__bridge_transfer HandmadeView *)window->native_view;
    }

    if (global_window_delegate) {
      global_window_delegate->platform_window = nullptr;
      global_window_delegate = nil;
    }

    window->native_window = nullptr;
    window->native_view = nullptr;
    window->running = false;
  }
}
