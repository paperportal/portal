const std = @import("std");
const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const display = sdk.display;
const gesture = sdk.gesture;

const allocator = std.heap.wasm_allocator;

var g_initialized: bool = false;

var g_handle_sleep: i32 = 0;
var g_handle_l: i32 = 0;
var g_handle_v: i32 = 0;
var g_handle_z: i32 = 0;

var g_last_msg_buf: [64]u8 = [_]u8{0} ** 64;

fn writeZ(buf: []u8, msg: []const u8) [:0]const u8 {
    if (buf.len == 0) return &[_:0]u8{};
    const max_copy = @min(msg.len, buf.len - 1);
    std.mem.copyForwards(u8, buf[0..max_copy], msg[0..max_copy]);
    buf[max_copy] = 0;
    return buf[0..max_copy :0];
}

fn drawScreen() void {
    display.epd.setMode(display.epd.QUALITY) catch {};
    display.startWrite() catch {};
    defer display.endWrite() catch {};

    display.fillScreen(display.colors.WHITE) catch {};

    display.text.setEncodingUtf8() catch {};
    display.text.setSize(1.0, 1.0) catch {};
    display.text.setColor(display.colors.BLACK, display.colors.WHITE) catch {};

    //display.text.drawCstr("Gesture demo", 12, 16) catch {};
    display.text.drawCstr("Draw one of these:", 12, 44) catch {};
    display.text.drawCstr("- L shape", 12, 68) catch {};
    display.text.drawCstr("- V shape", 12, 92) catch {};
    display.text.drawCstr("- Z shape", 12, 116) catch {};

    //var line_buf: [64]u8 = undefined;
    //const handles = std.fmt.bufPrint(&line_buf, "handles: L={} V={} Z={}", .{ g_handle_l, g_handle_v, g_handle_z }) catch "";
    //display.text.draw(handles, 12, 148) catch {};

    const msg = std.mem.sliceTo(&g_last_msg_buf, 0);
    if (msg.len > 0) {
        display.text.draw(msg, 12, 180) catch {};
    } else {
        display.text.drawCstr("Recognized: (none yet)", 12, 180) catch {};
    }

    display.update() catch {};
}

fn setLastMsg(msg: []const u8) void {
    _ = writeZ(g_last_msg_buf[0..], msg);
}

fn registerGestures() void {
    gesture.clearAll() catch {};

    const points_sleep = [_]gesture.PointF{
        .{ .x = 280, .y = 860 },
        .{ .x = 280, .y = 500 },
        .{ .x = 280, .y = 860 },
    };

    const points_l = [_]gesture.PointF{
        .{ .x = 0, .y = 0 },
        .{ .x = 0, .y = 200 },
        .{ .x = 200, .y = 140 },
    };

    const points_v = [_]gesture.PointF{
        .{ .x = 0, .y = 0 },
        .{ .x = 100, .y = 200 },
        .{ .x = 200, .y = 0 },
    };

    const points_z = [_]gesture.PointF{
        .{ .x = 0, .y = 0 },
        .{ .x = 200, .y = 0 },
        .{ .x = 0, .y = 200 },
        .{ .x = 200, .y = 200 },
    };

    const params: gesture.PolylineParams = .{
        .fixed = false,
        .tolerance_px = 100.0,
        .priority = 10,
        .max_duration_ms = 12500,
        .options = .{ .segment_constraint_enabled = true },
    };

    g_handle_sleep = gesture.registerPolyline("SLP", &points_sleep, .{
        .fixed = true,
        .tolerance_px = 100.0,
        .priority = 10,
        .max_duration_ms = 12500,
        .options = .{ .segment_constraint_enabled = true },
    }) catch 0;
    g_handle_l = gesture.registerPolyline("L", &points_l, params) catch 0;
    g_handle_v = gesture.registerPolyline("V", &points_v, params) catch 0;
    g_handle_z = gesture.registerPolyline("Z", &points_z, params) catch 0;

    if (g_handle_l == 0 or g_handle_v == 0 or g_handle_z == 0) {
        setLastMsg("Register failed (check last error)");
        return;
    }
}

pub export fn ppInit(api_version: i32, args_ptr: i32, args_len: i32) i32 {
    _ = api_version;
    _ = args_ptr;
    _ = args_len;

    if (g_initialized) return 0;
    g_initialized = true;

    core.begin() catch {
        return -1;
    };

    display.vlw.useSystem(display.vlw.SystemFont.inter, 12) catch {};

    registerGestures();
    drawScreen();

    core.log.finfo("gesture-demo: ready (L={} V={} Z={})", .{ g_handle_l, g_handle_v, g_handle_z });
    return 0;
}

pub export fn ppOnGesture(kind: i32, x: i32, y: i32, dx: i32, dy: i32, duration_ms: i32, now_ms: i32, flags: i32) i32 {
    _ = x;
    _ = y;
    _ = dx;
    _ = dy;
    _ = duration_ms;
    _ = now_ms;

    if (kind != @intFromEnum(gesture.GestureKind.custom_polyline)) {
        return 0;
    }

    const handle = gesture.customPolylineHandle(flags);
    const name: []const u8 = if (handle == g_handle_l)
        "L"
    else if (handle == g_handle_v)
        "V"
    else if (handle == g_handle_z)
        "Z"
    else
        "?";

    var buf: [64]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, "Recognized: {s} (handle={})", .{ name, handle }) catch "Recognized";
    setLastMsg(msg);
    drawScreen();

    core.log.finfo("gesture-demo: recognized {s} (handle={})", .{ name, handle });
    return 0;
}

pub export fn ppShutdown() void {
    gesture.clearAll() catch {};
    display.vlw.clearAll() catch {};
}
