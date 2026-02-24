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
const ui = sdk.ui;

var g_initialized: bool = false;
var g_controller: ?Controller = null;
var g_controller_task_handle: i32 = 0;

pub fn main() !void {
    if (g_initialized) return;
    g_initialized = true;

    core.begin() catch |err| {
        core.log.ferr("main: core.begin failed: {s}", .{@errorName(err)});
        return err;
    };

    display.vlw.useSystem(display.vlw.SystemFont.inter, 12) catch {};

    const header_h = title_bar.draw() catch |err| {
        core.log.ferr("main: drawHeader failed: {s}", .{@errorName(err)});
        return err;
    };

    g_controller = Controller.init(allocator, header_h) catch |err| {
        core.log.ferr("main: controller init failed: {s}", .{@errorName(err)});
        return err;
    };

    if (g_controller) |*c| {
        ui.scene.set(ui.Scene.from(Controller, c)) catch |err| {
            core.log.ferr("main: ui.scene.set failed: {s}", .{@errorName(err)});
            return err;
        };
        g_controller_task_handle = microtask.start(microtask.Task.from(Controller, c), 0, 0) catch |err| {
            core.log.ferr("main: controller microtask start failed: {s}", .{@errorName(err)});
            g_controller_task_handle = 0;
            return err;
        };
    }
}

fn cancelHandle(handle: *i32) void {
    if (handle.* <= 0) return;
    microtask.cancel(handle.*) catch {};
    handle.* = 0;
}

pub export fn ppShutdown() void {
    cancelHandle(&g_controller_task_handle);
    microtask.clearAll() catch {};
    ui.scene.deinitStack();
    if (g_controller) |*c| {
        c.deinit();
        g_controller = null;
    }
    display.vlw.clearAll() catch {};
}
