const std = @import("std");
const sdk = @import("paper_portal_sdk");

const core = sdk.core;
const display = sdk.display;
const microtask = sdk.microtask;

const tap_kind: i32 = 1;
const heartbeat_period_ms: u32 = 500;
const delayed_once_ms: u32 = 2000;
const worker_chunk_size: u32 = 20;
const worker_total_steps: u32 = 240;
const worker_sleep_ms: u32 = 80;

const HeartbeatTask = struct {
    count: u32 = 0,

    pub fn step(self: *HeartbeatTask, now_ms: u32) anyerror!microtask.Action {
        self.count += 1;
        core.log.finfo("microtask-demo: heartbeat #{} at {}ms", .{ self.count, now_ms });
        return microtask.Action.yieldSoon();
    }

    pub fn onCancel(self: *HeartbeatTask) void {
        core.log.finfo("microtask-demo: heartbeat cancelled at count={}", .{self.count});
    }
};

const DelayedTask = struct {
    fired: bool = false,

    pub fn step(self: *DelayedTask, now_ms: u32) anyerror!microtask.Action {
        self.fired = true;
        core.log.finfo("microtask-demo: delayed one-shot fired at {}ms", .{now_ms});
        return microtask.Action.doneNow();
    }
};

const WorkerTask = struct {
    done: u32 = 0,

    pub fn step(self: *WorkerTask, now_ms: u32) anyerror!microtask.Action {
        var work: u32 = 0;
        while (work < worker_chunk_size and self.done < worker_total_steps) : (work += 1) {
            self.done += 1;
        }

        core.log.finfo("microtask-demo: worker progress {}/{} at {}ms", .{ self.done, worker_total_steps, now_ms });
        if (self.done >= worker_total_steps) {
            core.log.info("microtask-demo: worker finished");
            return microtask.Action.doneNow();
        }

        return microtask.Action.sleepMs(worker_sleep_ms);
    }

    pub fn onCancel(self: *WorkerTask) void {
        core.log.finfo("microtask-demo: worker cancelled at progress {}/{}", .{ self.done, worker_total_steps });
    }
};

var g_initialized: bool = false;
var g_tap_count: u32 = 0;

var g_heartbeat = HeartbeatTask{};
var g_delayed = DelayedTask{};
var g_worker = WorkerTask{};

var g_heartbeat_handle: i32 = 0;
var g_delayed_handle: i32 = 0;
var g_worker_handle: i32 = 0;

fn drawUi() void {
    display.epd.setMode(display.epd.FAST) catch {};
    display.startWrite() catch {};
    defer display.endWrite() catch {};

    display.fillScreen(display.colors.WHITE) catch {};
    display.text.setEncodingUtf8() catch {};
    display.text.setColor(display.colors.BLACK, display.colors.WHITE) catch {};

    display.text.setSize(3, 3) catch {};

    display.text.drawCstr("MicroTask demo", 12, 16) catch {};
    display.text.drawCstr("Heartbeat: 500ms periodic", 12, 78) catch {};
    display.text.drawCstr("Delayed one-shot: 2000ms", 12, 108) catch {};
    display.text.drawCstr("Chunked worker: 20 steps/80ms", 12, 138) catch {};

    var buf: [80]u8 = undefined;
    const line = std.fmt.bufPrint(&buf, "Tap count: {}", .{g_tap_count}) catch "";
    display.text.draw(line, 12, 184) catch {};

    display.update() catch {};
}

fn startTasks() void {
    g_heartbeat_handle = microtask.start(microtask.Task.from(HeartbeatTask, &g_heartbeat), 0, heartbeat_period_ms) catch |err| {
        core.log.ferr("microtask-demo: heartbeat start failed: {s}", .{@errorName(err)});
        return;
    };
    core.log.finfo("microtask-demo: heartbeat started handle={}", .{g_heartbeat_handle});

    g_delayed_handle = microtask.start(microtask.Task.from(DelayedTask, &g_delayed), delayed_once_ms, 0) catch |err| {
        core.log.ferr("microtask-demo: delayed start failed: {s}", .{@errorName(err)});
        return;
    };
    core.log.finfo("microtask-demo: delayed started handle={}", .{g_delayed_handle});

    g_worker_handle = microtask.start(microtask.Task.from(WorkerTask, &g_worker), 0, 0) catch |err| {
        core.log.ferr("microtask-demo: worker start failed: {s}", .{@errorName(err)});
        return;
    };
    core.log.finfo("microtask-demo: worker started handle={}", .{g_worker_handle});
}

fn cancelHandle(handle: *i32) void {
    if (handle.* <= 0) return;
    microtask.cancel(handle.*) catch {};
    handle.* = 0;
}

pub fn main() !void {
    if (g_initialized) return;
    g_initialized = true;

    core.begin() catch |err| {
        core.log.ferr("main: core.begin failed: {s}", .{@errorName(err)});
        return err;
    };

    drawUi();
    startTasks();
    core.log.info("microtask-demo: initialized (tap screen while worker runs)");
}

pub export fn ppOnGesture(kind: i32, x: i32, y: i32, dx: i32, dy: i32, duration_ms: i32, now_ms: i32, flags: i32) i32 {
    _ = dx;
    _ = dy;
    _ = duration_ms;
    _ = flags;

    if (kind == tap_kind) {
        g_tap_count += 1;
        core.log.finfo("microtask-demo: tap #{} at ({}, {}) now={}ms", .{ g_tap_count, x, y, now_ms });
        drawUi();
    }
    return 0;
}

pub export fn ppShutdown() void {
    cancelHandle(&g_heartbeat_handle);
    cancelHandle(&g_delayed_handle);
    cancelHandle(&g_worker_handle);
    microtask.clearAll() catch {};
    g_initialized = false;
    core.log.info("microtask-demo: shutdown");
}
