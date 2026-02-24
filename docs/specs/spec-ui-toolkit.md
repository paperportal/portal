# UI toolkit specification

This file specifies the reusable UI widgets and layout components used by the Paper Portal launcher.

## Overview

The launcher implements a set of reusable UI widgets and layout components for building user interfaces on the e-ink display. These components are designed specifically for the constraints of e-ink displays (slow refresh, limited color) and touch input.

**Future consideration:** These components should eventually be extracted into a separate reusable package or integrated into the Zig SDK so that other apps can benefit from them.

## Design principles

| Principle | Description |
|-----------|-------------|
| E-ink optimized | Minimize full refreshes, use FAST mode for partial updates |
| Touch-friendly | Large tap targets (minimum 40x40px), clear visual feedback |
| High contrast | Use black/white primarily, grayscale only where necessary |
| Simple animations | Avoid animations that require frequent refreshes |
| Memory efficient | Avoid unnecessary allocations, reuse buffers where possible |

## Layout components

### Grid layout

Arranges items in a rows×columns grid with configurable spacing.

**Properties:**
- `rows: u32` - Number of rows
- `columns: u32` - Number of columns
- `item_width: i32` - Width of each item
- `item_height: i32` - Height of each item
- `spacing_x: i32` - Horizontal spacing between items
- `spacing_y: i32` - Vertical spacing between items
- `padding: Rect` - Padding around the grid

**Methods:**
- `layout(item_index: u32) Rect` - Calculate position rectangle for an item
- `item_at_position(x: i32, y: i32) ?u32` - Find which item was tapped

**Use case:** Main launcher app grid

### List layout

Arranges items vertically in a scrollable list.

**Properties:**
- `item_height: i32` - Height of each item
- `spacing: i32` - Spacing between items
- `padding: Rect` - Padding around the list

**Methods:**
- `layout(item_index: u32) Rect` - Calculate position rectangle for an item
- `scroll(offset: i32) void` - Scroll the list by offset pixels
- `item_at_position(x: i32, y: i32) ?u32` - Find which item was tapped

**Use case:** Settings menu, file browser

### Page view

Manages multiple pages of content with swipe/tap navigation.

**Properties:**
- `page_count: u32` - Total number of pages
- `current_page: u32` - Currently visible page

**Methods:**
- `next_page() void` - Go to next page
- `previous_page() void` - Go to previous page
- `set_page(index: u32) void` - Jump to specific page

**Use case:** App grid pagination

### Status bar

Fixed bar at the top of the screen showing device status.

**Content:**
- Left: Launcher logo/title
- Right: Status icons (battery, WiFi, SD card)

**Properties:**
- `height: i32` - Height of status bar (e.g., 32px)
- `background: Color` - Background color

**Methods:**
- `draw() Error!void` - Render the status bar

## Widgets

### Button

A tappable widget with text or icon.

**States:**
- `normal` - Default appearance
- `pressed` - Visual feedback while touching
- `disabled` - Grayed out, not tappable

**Properties:**
- `rect: Rect` - Position and size
- `text: []const u8` - Button label (optional)
- `icon: []const u8` - Icon data (optional)
- `state: State` - Current state

**Methods:**
- `draw() Error!void` - Render the button
- `handle_touch(x: i32, y: i32, pressed: bool) bool` - Returns true if tapped

**Refresh behavior:**
- Draw `normal` state using QUALITY mode
- Draw `pressed` state using FAST mode
- Restore to `normal` using FAST mode

### Label

Displays text with configurable font and alignment.

**Properties:**
- `rect: Rect` - Position and size
- `text: []const u8` - Text content
- `font: Font` - Font to use
- `alignment: Alignment` - left/center/right
- `color: Color` - Text color
- `wrap: bool` - Whether to wrap text

**Methods:**
- `draw() Error!void` - Render the label
- `measure_text() Size` - Calculate text dimensions

### Icon

Displays a monochrome icon image.

**Properties:**
- `rect: Rect` - Position and size
- `data: []const u8` - Icon bitmap data
- `color: Color` - Icon color

**Methods:**
- `draw() Error!void` - Render the icon

### Progress indicator

Shows a progress bar or spinner.

**Types:**
- `bar` - Horizontal progress bar
- `spinner` - Simple rotating indicator

**Properties (bar):**
- `rect: Rect` - Position and size
- `progress: f32` - Progress value (0.0 to 1.0)

**Methods:**
- `draw() Error!void` - Render the progress indicator

### QR code display

Displays a QR code with optional caption.

**Properties:**
- `rect: Rect` - Position and size
- `data: [][]bool` - QR code bitmap
- `caption: []const u8` - Optional text below QR code

**Methods:**
- `draw() Error!void` - Render the QR code (using FAST mode for initial draw)

**Use case:** Web config/dev server connection screen

## Input handling

### Touch manager

Centralized touch input handling for widgets.

**Methods:**
- `poll() Error!?TouchEvent` - Get next touch event
- `register_widget(widget: *Widget) void` - Register a widget for touch handling
- `unregister_widget(widget: *Widget) void` - Unregister a widget

**Event types:**
```zig
pub const TouchEvent = struct {
    x: i32,
    y: i32,
    action: Action,
};

pub const Action = enum {
    press,   // Finger touched screen
    release, // Finger lifted
    move,    // Finger moved
};
```

### Gesture callback contract

Apps receive touch/gesture input via the host callback `portalGesture(kind, x, y, dx, dy, duration_ms, now_ms, flags)`.

- Built-in kinds (tap/drag/long-press/flick) may be emitted as the touch sequence progresses.
- Custom polyline gestures (registered via the `m5_gesture` WASM imports) emit **only on Up** when recognized:
  - `kind = 100` (`kGestureCustomPolyline`)
  - `flags` carries the winning gesture handle returned at registration

## Refresh strategy

### E-ink refresh modes

| Mode | Use case | Quality | Speed |
|------|----------|---------|-------|
| QUALITY | Initial screen draw, major changes | High | Slow |
| FAST | Minor updates, button presses | Medium | Fast |
| FASTEST | Very frequent updates (avoid) | Low | Very fast |
| TEXT | Text-only content | High (text) | Medium |

### Refresh guidelines

| Scenario | Mode |
|----------|------|
| First screen draw | QUALITY |
| Button press | FAST |
| Page navigation | FAST (then QUALITY after delay) |
| Progress updates | FASTEST (only if necessary) |

### Partial refresh optimization

Use `display.update_rect()` for partial screen updates when only a small area changed:

```zig
// Only refresh the button area
try display.update_rect(button.rect.x, button.rect.y, button.rect.width, button.rect.height);
```

## Architecture

### Component structure

```zig
// Base trait for all widgets
pub const Widget = struct {
    rect: Rect,
    visible: bool,

    fn draw(self: *const Widget) Error!void {
        _ = self;
        // Implemented by each widget type
    }

    fn handle_touch(self: *Widget, event: TouchEvent) bool {
        _ = self;
        _ = event;
        // Returns true if event was handled
        return false;
    }
};
```

### Screen management

```zig
pub const Screen = struct {
    widgets: std.ArrayList(*Widget),

    fn draw(self: *Screen) Error!void {
        try display.clear();
        for (self.widgets.items) |widget| {
            if (widget.visible) try widget.draw();
        }
        try display.update();
    }

    fn handle_touch(self: *Screen, event: TouchEvent) !bool {
        for (self.widgets.items) |widget| {
            if (widget.visible and widget.handle_touch(event)) {
                return true;
            }
        }
        return false;
    }
};
```

## Future extraction plan

These UI components should eventually be extracted into:

**Option A:** Zig SDK (`/Users/mika/code/paperportal/zig-sdk/sdk/ui.zig`)
- Pros: Available to all apps by default
- Cons: Bloats the SDK

**Option B:** Separate package (`paperportal-ui`)
- Pros: Can be versioned independently
- Cons: Requires dependency management

**Recommended:** Start in launcher, extract to separate package once mature.
