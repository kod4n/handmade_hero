#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <GameController/GameController.h>

#include "macos_platform.h"

using namespace hh;

// macOS virtual key codes (kVK_ANSI_* in <Carbon/HIToolbox/Events.h>).
enum MacOSKeyCode : u16 {
  k_key_a = 0x00,
  k_key_s = 0x01,
  k_key_d = 0x02,
  k_key_c = 0x08,
  k_key_q = 0x0C,
  k_key_w = 0x0D,
  k_key_e = 0x0E,
  k_key_left = 0x7B,  // kVK_LeftArrow
  k_key_right = 0x7C, // kVK_RightArrow
  k_key_down = 0x7D,  // kVK_DownArrow
  k_key_up = 0x7E,    // kVK_UpArrow
};

// Keys that the window handles itself (kVK_Return, kVK_ANSI_F).
static constexpr u16 k_key_return = 0x24;
static constexpr u16 k_key_f = 0x03;

// Keyboard controller being filled during macos_window_pump_events.
static ControllerInput *global_kb = nullptr;

#if DEV
// The DEV window floats above other apps so it can sit over the editor, and
// fades while another app has focus (the Windows build does the same).
static constexpr CGFloat k_dev_unfocused_alpha = 64.0 / 255.0;
#endif

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
  case k_key_c:
    macos_process_kb_button(input_action(kb, Action::Start), is_down);
    break;
  case k_key_up:
    macos_process_kb_button(input_action(kb, Action::ActionUp), is_down);
    break;
  case k_key_down:
    macos_process_kb_button(input_action(kb, Action::ActionDown), is_down);
    break;
  case k_key_left:
    macos_process_kb_button(input_action(kb, Action::ActionLeft), is_down);
    break;
  case k_key_right:
    macos_process_kb_button(input_action(kb, Action::ActionRight), is_down);
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
    new_input.stick_avg.x =
        macos_normalize_stick(pad.leftThumbstick.xAxis.value, dead_zone);
    new_input.stick_avg.y =
        macos_normalize_stick(pad.leftThumbstick.yAxis.value, dead_zone);

    // dpad, overrides joystick when used
    if (pad.dpad.up.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg.y = 1.0f;
    }
    if (pad.dpad.down.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg.y = -1.0f;
    }
    if (pad.dpad.right.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg.x = 1.0f;
    }
    if (pad.dpad.left.isPressed) {
      new_input.is_analog = false;
      new_input.stick_avg.x = -1.0f;
    }

    constexpr f32 move_threshold = 0.5f;
    macos_process_pad_button(new_input.stick_avg.x < -move_threshold,
                             input_action(old_input, Action::MoveLeft),
                             input_action(new_input, Action::MoveLeft));
    macos_process_pad_button(new_input.stick_avg.x > move_threshold,
                             input_action(old_input, Action::MoveRight),
                             input_action(new_input, Action::MoveRight));
    macos_process_pad_button(new_input.stick_avg.y < -move_threshold,
                             input_action(old_input, Action::MoveDown),
                             input_action(new_input, Action::MoveDown));
    macos_process_pad_button(new_input.stick_avg.y > move_threshold,
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
    macos_process_pad_button(pad.buttonMenu.isPressed,
                             input_action(old_input, Action::Start),
                             input_action(new_input, Action::Start));
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

@interface HandmadeWindowDelegate : NSObject <NSWindowDelegate> {
@public
  MacOSWindow *platform_window;
}
@end

static HandmadeWindowDelegate *global_window_delegate;

// A transparent cursor, used to hide the cursor over the game in non-DEV
// builds while keeping the normal cursor over the title bar.
[[maybe_unused]] static NSCursor *macos_blank_cursor() {
  static NSCursor *cursor = nil;
  if (!cursor) {
    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(1, 1)];
    cursor = [[NSCursor alloc] initWithImage:image hotSpot:NSZeroPoint];
  }
  return cursor;
}

@implementation HandmadeView

- (BOOL)acceptsFirstResponder {
  return YES;
}

// Top-left origin, matching display_rect and the game's top-down rows.
- (BOOL)isFlipped {
  return YES;
}

- (void)resetCursorRects {
  if constexpr (!k_dev_mode) {
    [self addCursorRect:self.bounds cursor:macos_blank_cursor()];
  }
}

- (void)drawRect:(NSRect)dirty_rect {
  (void)dirty_rect;

  [[NSColor blackColor] setFill];
  NSRectFill(self.bounds);

  if (!pixels || pixel_width == 0 || pixel_height == 0) {
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
      // Bounds are in points: on a Retina display the 1:1 case still covers
      // 2x2 device pixels per game pixel, scaled with nearest-neighbor.
      const DisplayRect dest = display_rect(
          static_cast<i32>(pixel_width), static_cast<i32>(pixel_height),
          static_cast<i32>(NSWidth(self.bounds)),
          static_cast<i32>(NSHeight(self.bounds)));

      // respectFlipped:YES draws the image upright in this flipped view, so
      // the first row of the buffer ends up at the top.
      NSImage *ns_image = [[NSImage alloc] initWithCGImage:image
                                                      size:NSZeroSize];
      NSGraphicsContext *context = [NSGraphicsContext currentContext];
      [context saveGraphicsState];
      [context setImageInterpolation:NSImageInterpolationNone];
      [ns_image drawInRect:NSMakeRect(dest.x, dest.y, dest.width, dest.height)
                  fromRect:NSZeroRect
                 operation:NSCompositingOperationCopy
                  fraction:1.0
            respectFlipped:YES
                     hints:nil];
      [context restoreGraphicsState];
      CGImageRelease(image);
    }

    CGDataProviderRelease(provider);
  }

  CGColorSpaceRelease(color_space);
}

- (void)keyDown:(NSEvent *)event {
  if (event.isARepeat) {
    return;
  }

  // Window shortcuts, handled here instead of going to the game:
  //   Cmd+Q                 quit (there's no menu bar to provide it)
  //   Option+Return         toggle fullscreen (like Alt+Enter on Windows)
  //   Ctrl+Cmd+F            toggle fullscreen (the standard macOS shortcut)
  const NSEventModifierFlags mods =
      event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  const bool cmd = mods & NSEventModifierFlagCommand;
  const bool ctrl = mods & NSEventModifierFlagControl;
  const bool option = mods & NSEventModifierFlagOption;
  if (cmd && event.keyCode == k_key_q) {
    if (global_window_delegate && global_window_delegate->platform_window) {
      global_window_delegate->platform_window->running = false;
    }
    return;
  }
  if ((option && event.keyCode == k_key_return) ||
      (ctrl && cmd && event.keyCode == k_key_f)) {
    [self.window toggleFullScreen:nil];
    return;
  }

  macos_process_key(event.keyCode, true);
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

#if DEV
- (void)windowDidBecomeKey:(NSNotification *)notification {
  NSWindow *window = notification.object;
  [window setAlphaValue:1.0];
}

- (void)windowDidResignKey:(NSNotification *)notification {
  NSWindow *window = notification.object;
  // Fullscreen has its own space, so there's nothing behind it to show.
  if (!(window.styleMask & NSWindowStyleMaskFullScreen)) {
    [window setAlphaValue:k_dev_unfocused_alpha];
  }
}

// A floating window can't take part in a fullscreen space, so it's a normal
// window while fullscreen and floats again afterwards.
- (void)windowWillEnterFullScreen:(NSNotification *)notification {
  NSWindow *window = notification.object;
  [window setLevel:NSNormalWindowLevel];
  [window setAlphaValue:1.0];
}

- (void)windowDidExitFullScreen:(NSNotification *)notification {
  NSWindow *window = notification.object;
  [window setLevel:NSFloatingWindowLevel];
}
#endif

@end

#if DEV
// Where the DEV window opens: flush against the right edge of the screen the
// mouse is on (the one you're working on), vertically centered, so the editor
// stays visible on the left. Cocoa screen coordinates have a bottom-left
// origin; visibleFrame excludes the menu bar and Dock.
static NSPoint macos_dev_window_origin(NSSize frame_size) {
  NSScreen *screen = NSScreen.mainScreen;
  const NSPoint mouse = NSEvent.mouseLocation;
  for (NSScreen *candidate in NSScreen.screens) {
    if (NSMouseInRect(mouse, candidate.frame, NO)) {
      screen = candidate;
      break;
    }
  }
  const NSRect visible = screen.visibleFrame;
  return NSMakePoint(
      MAX(NSMinX(visible), NSMaxX(visible) - frame_size.width),
      MAX(NSMinY(visible),
          NSMinY(visible) + (NSHeight(visible) - frame_size.height) / 2));
}
#endif

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
    [native_window setAcceptsMouseMovedEvents:YES];
    [native_window
        setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
#if DEV
    [native_window setLevel:NSFloatingWindowLevel];
    [native_window
        setFrameOrigin:macos_dev_window_origin(native_window.frame.size)];
#else
    [native_window center];
#endif

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

void macos_window_set_title(MacOSWindow *window, const char *title) {
  if (!window || !window->native_window || !title) {
    return;
  }

  @autoreleasepool {
    NSWindow *native_window = (__bridge NSWindow *)window->native_window;
    NSString *ns_title = [NSString stringWithUTF8String:title];
    if (ns_title) {
      [native_window setTitle:ns_title];
    }
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
