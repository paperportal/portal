const std = @import("std");

const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const fs = sdk.fs;
const Error = sdk.errors.Error;

const paths = @import("paths.zig");

pub const AppEntry = struct {
    id: []const u8,
    name: []const u8,
    manifest_json: []const u8,
    pinned_publisher_pubkey_b64: ?[]const u8 = null,
};

pub const Catalog = struct {
    schema_version: u32 = 1,
    apps: std.ArrayListUnmanaged(AppEntry) = .{},

    pub fn deinit(self: *Catalog, allocator: std.mem.Allocator) void {
        for (self.apps.items) |e| {
            allocator.free(e.id);
            allocator.free(e.name);
            allocator.free(e.manifest_json);
            if (e.pinned_publisher_pubkey_b64) |s| allocator.free(s);
        }
        self.apps.deinit(allocator);
        self.* = .{};
    }

    pub fn removeAt(self: *Catalog, allocator: std.mem.Allocator, index: usize) void {
        const e = self.apps.items[index];
        allocator.free(e.id);
        allocator.free(e.name);
        allocator.free(e.manifest_json);
        if (e.pinned_publisher_pubkey_b64) |s| allocator.free(s);
        _ = self.apps.orderedRemove(index);
    }
};

fn dupeZNoSentinel(allocator: std.mem.Allocator, s: []const u8) ![]u8 {
    return allocator.dupe(u8, s);
}

fn readAllFile(allocator: std.mem.Allocator, path_z: [:0]const u8) !?[]u8 {
    const meta = fs.metadata(path_z) catch |err| switch (err) {
        Error.NotFound => return null,
        else => return err,
    };
    const size: usize = std.math.cast(usize, meta.size) orelse return Error.Internal;
    var out = try allocator.alloc(u8, size);
    errdefer allocator.free(out);

    var f = fs.File.open(path_z, fs.FS_READ) catch |err| switch (err) {
        Error.NotFound => return null,
        else => return err,
    };
    defer f.close() catch {};

    var filled: usize = 0;
    while (filled < out.len) {
        const n = try f.read(out[filled..]);
        if (n == 0) break;
        filled += n;
    }
    if (filled != out.len) return Error.Internal;
    return out;
}

fn jsonStringifyAlloc(allocator: std.mem.Allocator, v: std.json.Value) ![]u8 {
    return std.json.Stringify.valueAlloc(allocator, v, .{});
}

fn jsonGetString(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    const v = obj.get(key) orelse return null;
    return switch (v) {
        .string => |s| s,
        else => null,
    };
}

fn jsonGetObject(obj: std.json.ObjectMap, key: []const u8) ?std.json.ObjectMap {
    const v = obj.get(key) orelse return null;
    return switch (v) {
        .object => |o| o,
        else => null,
    };
}

pub fn load(allocator: std.mem.Allocator, path_z: [:0]const u8) !struct { catalog: Catalog, was_missing: bool } {
    var catalog: Catalog = .{};
    errdefer catalog.deinit(allocator);

    const file_bytes_opt = readAllFile(allocator, path_z) catch |err| {
        var buf: [192]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "apps.json: read failed ({s}); treating as empty",
            .{@errorName(err)},
        ) catch "apps.json: read failed; treating as empty";
        core.log.warn(msg);
        return .{ .catalog = catalog, .was_missing = true };
    };
    if (file_bytes_opt == null) {
        return .{ .catalog = catalog, .was_missing = true };
    }
    const file_bytes = file_bytes_opt.?;
    defer allocator.free(file_bytes);

    const parsed = std.json.parseFromSlice(std.json.Value, allocator, file_bytes, .{}) catch |err| {
        var buf: [192]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "apps.json: parse failed ({s}); treating as empty",
            .{@errorName(err)},
        ) catch "apps.json: parse failed; treating as empty";
        core.log.info(msg);
        return .{ .catalog = catalog, .was_missing = true };
    };
    defer parsed.deinit();

    if (parsed.value != .object) {
        core.log.info("apps.json: root not object; treating as empty");
        return .{ .catalog = catalog, .was_missing = true };
    }
    const root = parsed.value.object;

    const schema_v = root.get("schema_version") orelse {
        core.log.info("apps.json: missing schema_version; treating as empty");
        return .{ .catalog = catalog, .was_missing = true };
    };
    const schema_ok = switch (schema_v) {
        .integer => |i| i == 1,
        else => false,
    };
    if (!schema_ok) {
        core.log.info("apps.json: unsupported schema_version; treating as empty");
        return .{ .catalog = catalog, .was_missing = true };
    }

    const apps_val = root.get("apps") orelse {
        return .{ .catalog = catalog, .was_missing = false };
    };
    if (apps_val != .array) {
        core.log.info("apps.json: apps not array; treating as empty");
        return .{ .catalog = catalog, .was_missing = true };
    }

    for (apps_val.array.items) |item| {
        if (item != .object) continue;
        const item_obj = item.object;

        const id = jsonGetString(item_obj, "id") orelse continue;
        const manifest_obj = jsonGetObject(item_obj, "manifest") orelse continue;
        const name = jsonGetString(manifest_obj, "name") orelse "";

        const pinned = jsonGetString(item_obj, "pinned_publisher_pubkey");

        const manifest_json = jsonStringifyAlloc(allocator, .{ .object = manifest_obj }) catch continue;
        errdefer allocator.free(manifest_json);

        const entry: AppEntry = .{
            .id = try dupeZNoSentinel(allocator, id),
            .name = try dupeZNoSentinel(allocator, name),
            .manifest_json = manifest_json,
            .pinned_publisher_pubkey_b64 = if (pinned) |p| try dupeZNoSentinel(allocator, p) else null,
        };

        try catalog.apps.append(allocator, entry);
    }

    return .{ .catalog = catalog, .was_missing = false };
}

fn writeAll(file: *fs.File, bytes: []const u8) !void {
    var off: usize = 0;
    while (off < bytes.len) {
        const n = try file.write(bytes[off..]);
        if (n == 0) return Error.Internal;
        off += n;
    }
}

fn lastHostErrorMessage(buf: []u8) []const u8 {
    return core.lastErrorMessage(buf) catch "";
}

fn sortAppsByNameThenId(apps: []AppEntry) void {
    const Ctx = struct {};
    std.sort.pdq(AppEntry, apps, Ctx{}, struct {
        fn lessThan(_: Ctx, a: AppEntry, b: AppEntry) bool {
            const name_cmp = std.mem.order(u8, a.name, b.name);
            if (name_cmp == .lt) return true;
            if (name_cmp == .gt) return false;
            return std.mem.order(u8, a.id, b.id) == .lt;
        }
    }.lessThan);
}

pub fn saveAtomic(catalog: *Catalog, allocator: std.mem.Allocator, path_z: [:0]const u8) !void {
    sortAppsByNameThenId(catalog.apps.items);

    var out: std.Io.Writer.Allocating = .init(allocator);
    defer out.deinit();

    const writer = &out.writer;
    const enc = std.json.Stringify;
    const enc_opts: enc.Options = .{ .escape_unicode = false };

    try writer.writeAll("{\"schema_version\":1,\"apps\":[");
    for (catalog.apps.items, 0..) |app, idx| {
        if (idx != 0) try writer.writeAll(",");
        try writer.writeAll("{\"id\":");
        try enc.encodeJsonString(app.id, enc_opts, writer);
        try writer.writeAll(",\"manifest\":");
        try writer.writeAll(app.manifest_json);
        if (app.pinned_publisher_pubkey_b64) |pinned| {
            try writer.writeAll(",\"pinned_publisher_pubkey\":");
            try enc.encodeJsonString(pinned, enc_opts, writer);
        }
        try writer.writeAll("}");
    }
    try writer.writeAll("]}");

    var tmp_buf: [256]u8 = undefined;
    const tmp_z = std.fmt.bufPrintZ(&tmp_buf, "{s}.tmp", .{path_z}) catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "apps.json: save failed (tmp path) ({s}) path={s}\n",
            .{ @errorName(err), path_z },
        ) catch "apps.json: save failed (tmp path)\n";
        core.log.warn(msg);
        return Error.InvalidArgument;
    };

    var tmp = fs.File.open(tmp_z, fs.FS_WRITE | fs.FS_CREATE | fs.FS_TRUNC) catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "apps.json: save failed (open tmp) ({s}) tmp={s}\n",
            .{ @errorName(err), tmp_z },
        ) catch "apps.json: save failed (open tmp)\n";
        core.log.warn(msg);
        return err;
    };
    var tmp_closed = false;
    defer {
        if (!tmp_closed) {
            tmp.close() catch |err| {
                var buf: [256]u8 = undefined;
                const msg = std.fmt.bufPrintZ(
                    &buf,
                    "apps.json: save failed (close tmp) ({s}) tmp={s}\n",
                    .{ @errorName(err), tmp_z },
                ) catch "apps.json: save failed (close tmp)\n";
                core.log.warn(msg);
            };
        }
    }

    writeAll(&tmp, out.written()) catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "apps.json: save failed (write tmp) ({s}) tmp={s} bytes={d}\n",
            .{ @errorName(err), tmp_z, out.written().len },
        ) catch "apps.json: save failed (write tmp)\n";
        core.log.warn(msg);
        return err;
    };

    tmp.close() catch |err| {
        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "apps.json: save failed (close tmp) ({s}) tmp={s}\n",
            .{ @errorName(err), tmp_z },
        ) catch "apps.json: save failed (close tmp)\n";
        core.log.warn(msg);
        return err;
    };
    tmp_closed = true;

    fs.rename(tmp_z, path_z) catch |err| {
        var host_err_buf: [128]u8 = undefined;
        const host_err = lastHostErrorMessage(host_err_buf[0..]);

        const dest_meta = fs.metadata(path_z) catch null;
        if (dest_meta != null) {
            fs.remove(path_z) catch |rm_err| {
                var rm_host_err_buf: [128]u8 = undefined;
                const rm_host_err = lastHostErrorMessage(rm_host_err_buf[0..]);
                var buf: [256]u8 = undefined;
                const msg = std.fmt.bufPrintZ(
                    &buf,
                    "apps.json: save failed (remove before rename) ({s}) path={s} host_err=\"{s}\"\n",
                    .{ @errorName(rm_err), path_z, rm_host_err },
                ) catch "apps.json: save failed (remove before rename)\n";
                core.log.warn(msg);
                return rm_err;
            };

            fs.rename(tmp_z, path_z) catch |retry_err| {
                var retry_host_err_buf: [128]u8 = undefined;
                const retry_host_err = lastHostErrorMessage(retry_host_err_buf[0..]);
                var buf: [256]u8 = undefined;
                const msg = std.fmt.bufPrintZ(
                    &buf,
                    "apps.json: save failed (rename retry) ({s}) from={s} to={s} host_err=\"{s}\"\n",
                    .{ @errorName(retry_err), tmp_z, path_z, retry_host_err },
                ) catch "apps.json: save failed (rename retry)\n";
                core.log.warn(msg);
                return retry_err;
            };

            return;
        }

        var buf: [256]u8 = undefined;
        const msg = std.fmt.bufPrintZ(
            &buf,
            "apps.json: save failed (rename) ({s}) from={s} to={s} host_err=\"{s}\"\n",
            .{ @errorName(err), tmp_z, path_z, host_err },
        ) catch "apps.json: save failed (rename)\n";
        core.log.warn(msg);
        return err;
    };
}

pub fn ensureBaseDirs() Error!void {
    fs.Dir.mkdir("/sdcard/portal") catch {};
    fs.Dir.mkdir(paths.apps_dir_z) catch {};
}
