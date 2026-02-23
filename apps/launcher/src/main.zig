const std = @import("std");
const sdk = @import("paper_portal_sdk");
const title_bar = @import("ui/title_bar.zig");
const core = sdk.core;
const display = sdk.display;
const microtask = sdk.microtask;
const Error = sdk.errors.Error;

const header_png_bytes = @embedFile("assets/main-header.png");

const allocator = std.heap.wasm_allocator;

const Controller = @import("controller.zig").Controller;

var g_initialized: bool = false;
var g_controller: ?Controller = null;
var g_controller_task_handle: i32 = 0;

fn cancelHandle(handle: *i32) void {
    if (handle.* <= 0) return;
    microtask.cancel(handle.*) catch {};
    handle.* = 0;
}

pub export fn ppInit(api_version: i32, args_ptr: i32, args_len: i32) i32 {
    _ = api_version;
    _ = args_ptr;
    _ = args_len;

    if (g_initialized) return 0;
    g_initialized = true;

    core.begin() catch {
        core.log.err("ppInit: core.begin failed");
        return -1;
    };

    display.vlw.useSystem(display.vlw.SystemFont.inter, 12) catch {};

    const header_h = title_bar.draw() catch {
        core.log.err("ppInit: drawHeader failed");
        return -1;
    };

    g_controller = Controller.init(allocator, header_h) catch {
        core.log.err("ppInit: controller init failed");
        return -1;
    };

    if (g_controller) |*c| {
        g_controller_task_handle = microtask.start(microtask.Task.from(Controller, c), 0, 0) catch |err| {
            core.log.ferr("ppInit: controller microtask start failed: {s}", .{@errorName(err)});
            g_controller_task_handle = 0;
            return -1;
        };
    }
    return 0;
}

pub export fn ppOnGesture(kind: i32, x: i32, y: i32, dx: i32, dy: i32, duration_ms: i32, now_ms: i32, flags: i32) i32 {
    _ = dx;
    _ = dy;
    _ = duration_ms;
    _ = now_ms;
    _ = flags;
    if (kind == 1) {
        if (g_controller) |*c| {
            c.onTap(x, y);
        }
    }
    return 0;
}

pub export fn ppShutdown() void {
    cancelHandle(&g_controller_task_handle);
    microtask.clearAll() catch {};
    if (g_controller) |*c| {
        c.deinit();
        g_controller = null;
    }
    display.vlw.clearAll() catch {};
}
