const std = @import("std");

const sdk = @import("paper_portal_sdk");
const core = sdk.core;
const fs = sdk.fs;
const Error = sdk.errors.Error;

fn logInfo(comptime fmt: []const u8, args: anytype) void {
    var buf: [384]u8 = undefined;
    const msg = std.fmt.bufPrintZ(&buf, fmt, args) catch return;
    core.log.info(msg);
}

pub const ZipError = error{
    InvalidZip,
    UnsupportedZip,
    InvalidEntryName,
    MissingManifest,
    MissingWasm,
    ChecksumMismatch,
    Io,
    OutOfMemory,
};

pub const Method = enum(u16) {
    store = 0,
    deflate = 8,
};

pub const Entry = struct {
    name: []u8,
    method: Method,
    flags: u16,
    comp_size: u32,
    uncomp_size: u32,
    data_offset: i32,
};

const kLocalHeaderSig: u32 = 0x04034b50;
const kGpFlagEncrypted: u16 = 0x0001;
const kGpFlagDataDescriptor: u16 = 0x0008;
// Deflate back-references need a 32 KiB history window, and match copies can
// request up to 258 bytes in one operation.
const kDeflateMaxMatchLen: usize = 258;
const kDeflateWriterBufLen: usize = std.compress.flate.history_len + kDeflateMaxMatchLen;

fn readExact(file: *fs.File, buf: []u8) !void {
    var off: usize = 0;
    while (off < buf.len) {
        const n = try file.read(buf[off..]);
        if (n == 0) return Error.Internal;
        off += n;
    }
}

fn readU16(file: *fs.File) !u16 {
    var b: [2]u8 = undefined;
    try readExact(file, b[0..]);
    return std.mem.readInt(u16, b[0..], .little);
}

fn readU32(file: *fs.File) !u32 {
    var b: [4]u8 = undefined;
    try readExact(file, b[0..]);
    return std.mem.readInt(u32, b[0..], .little);
}

fn validateEntryName(name: []const u8) ZipError!void {
    if (name.len == 0) return error.InvalidEntryName;
    if (name[0] == '/') return error.InvalidEntryName;
    if (std.mem.startsWith(u8, name, "./")) return error.InvalidEntryName;
    if (std.mem.indexOfScalar(u8, name, '\\') != null) return error.InvalidEntryName;

    var it = std.mem.splitScalar(u8, name, '/');
    while (it.next()) |seg| {
        if (seg.len == 0) return error.InvalidEntryName;
        if (std.mem.eql(u8, seg, ".") or std.mem.eql(u8, seg, "..")) return error.InvalidEntryName;
    }
}

fn nextEntry(allocator: std.mem.Allocator, file: *fs.File) !?Entry {
    const sig = readU32(file) catch return null;
    if (sig != kLocalHeaderSig) {
        return null;
    }

    _ = try readU16(file); // version needed
    const flags = try readU16(file);
    const method_raw = try readU16(file);
    _ = try readU16(file); // mtime
    _ = try readU16(file); // mdate
    _ = try readU32(file); // crc32
    const comp_size = try readU32(file);
    const uncomp_size = try readU32(file);
    const name_len = try readU16(file);
    const extra_len = try readU16(file);

    if ((flags & kGpFlagEncrypted) != 0) return Error.InvalidArgument;
    if ((flags & kGpFlagDataDescriptor) != 0) return Error.InvalidArgument;

    const method: Method = switch (method_raw) {
        0 => .store,
        8 => .deflate,
        else => return Error.InvalidArgument,
    };

    const name = try allocator.alloc(u8, name_len);
    errdefer allocator.free(name);
    try readExact(file, name);
    try validateEntryName(name);

    if (extra_len != 0) {
        _ = try file.seek(.{ .Current = @intCast(extra_len) });
    }

    const data_offset = try file.seek(.{ .Current = 0 });

    return .{
        .name = name,
        .method = method,
        .flags = flags,
        .comp_size = comp_size,
        .uncomp_size = uncomp_size,
        .data_offset = data_offset,
    };
}

fn skipEntryData(file: *fs.File, e: *const Entry) !void {
    _ = try file.seek(.{ .Current = @intCast(e.comp_size) });
}

fn mkdirP(path_z: [:0]const u8) void {
    const path: []const u8 = path_z;
    if (path.len == 0 or path[0] != '/') return;

    var buf: [256]u8 = undefined;
    var i: usize = 1;
    while (i <= path.len) : (i += 1) {
        if (i != path.len and path[i] != '/') continue;
        const slice = path[0..i];
        if (slice.len >= buf.len) return;
        @memcpy(buf[0..slice.len], slice);
        buf[slice.len] = 0;
        fs.Dir.mkdir(buf[0..slice.len :0]) catch {};
    }
}

const HashWriter = struct {
    file: *fs.File,
    sha_opt: ?*std.crypto.hash.sha2.Sha256 = null,
    written: u64 = 0,

    interface: std.Io.Writer = .{
        .vtable = &.{
            .drain = drain,
            .sendFile = sendFile,
        },
        .buffer = &.{},
    },

    fn drain(w: *std.Io.Writer, data: []const []const u8, splat: usize) std.Io.Writer.Error!usize {
        const self: *HashWriter = @alignCast(@fieldParentPtr("interface", w));
        // First drain any buffered bytes.
        const buffered = w.buffer[0..w.end];
        if (buffered.len != 0) {
            writeAll(self.file, buffered) catch return error.WriteFailed;
            if (self.sha_opt) |sha| sha.update(buffered);
            self.written += buffered.len;
            w.end = 0;
        }

        if (data.len == 0) return 0;

        var total: usize = 0;
        if (data.len > 1) {
            for (data[0 .. data.len - 1]) |chunk| {
                if (chunk.len == 0) continue;
                writeAll(self.file, chunk) catch return error.WriteFailed;
                if (self.sha_opt) |sha| sha.update(chunk);
                self.written += chunk.len;
                total += chunk.len;
            }
        }

        const pattern = data[data.len - 1];
        const reps: usize = if (splat == 0) 0 else splat;
        if (pattern.len != 0 and reps != 0) {
            var i: usize = 0;
            while (i < reps) : (i += 1) {
                writeAll(self.file, pattern) catch return error.WriteFailed;
                if (self.sha_opt) |sha| sha.update(pattern);
                self.written += pattern.len;
                total += pattern.len;
            }
        } else if (reps != 0) {
            // Even if there's nothing to write, consume the requested repetitions.
        }

        return total;
    }

    fn sendFile(
        _: *std.Io.Writer,
        _: *std.Io.File.Reader,
        _: std.Io.Limit,
    ) std.Io.Writer.FileError!usize {
        return error.Unimplemented;
    }
};

fn writeAll(file: *fs.File, bytes: []const u8) !void {
    var off: usize = 0;
    while (off < bytes.len) {
        const n = try file.write(bytes[off..]);
        if (n == 0) return Error.Internal;
        off += n;
    }
}

const FileReader = struct {
    file: *fs.File,
    reader: std.Io.Reader,
    buf: [4096]u8 = undefined,
    eof_logged: bool = false,
    zero_want_logged: bool = false,
    total_read: u64 = 0,
    log_every_read: u64 = 16 * 1024,
    next_read_log_at: u64 = 16 * 1024,

    const vtable: std.Io.Reader.VTable = .{
        .stream = stream,
    };

    pub fn init(file: *fs.File) FileReader {
        var self: FileReader = .{
            .file = file,
            .reader = undefined,
        };
        self.reader = .{
            .vtable = &vtable,
            .buffer = self.buf[0..],
            .seek = 0,
            .end = 0,
        };
        return self;
    }

    fn stream(r: *std.Io.Reader, w: *std.Io.Writer, limit: std.Io.Limit) std.Io.Reader.StreamError!usize {
        const self: *FileReader = @alignCast(@fieldParentPtr("reader", r));
        const want = limit.minInt(self.buf.len);
        if (want == 0) {
            if (!self.zero_want_logged) {
                const off = self.file.seek(.{ .Current = 0 }) catch -1;
                logInfo(
                    "zip: stream called with want=0 limit={d} off={d}",
                    .{ @intFromEnum(limit), off },
                );
                self.zero_want_logged = true;
            }
            return 0;
        }
        const tmp = self.buf[0..want];
        const n = self.file.read(tmp) catch |err| {
            logInfo("zip: read failed err={s}", .{@errorName(err)});
            return error.ReadFailed;
        };
        if (n == 0) {
            if (!self.eof_logged) {
                const off = self.file.seek(.{ .Current = 0 }) catch -1;
                logInfo("zip: unexpected EOF while streaming file off={d}", .{off});
                self.eof_logged = true;
            }
            return error.EndOfStream;
        }
        self.total_read += n;
        if (self.total_read >= self.next_read_log_at) {
            self.next_read_log_at = self.total_read + self.log_every_read;
        }
        w.writeAll(tmp[0..n]) catch return error.WriteFailed;
        return n;
    }
};

fn extractStored(
    file: *fs.File,
    e: *const Entry,
    entry_name: []const u8,
    out_file: *fs.File,
    sha_opt: ?*std.crypto.hash.sha2.Sha256,
) !u64 {
    if (e.comp_size != e.uncomp_size) return Error.InvalidArgument;
    var remaining: u32 = e.comp_size;
    var buf: [4096]u8 = undefined;
    var written: u64 = 0;
    while (remaining != 0) {
        const want: usize = @intCast(@min(@as(u32, buf.len), remaining));
        const n = try file.read(buf[0..want]);
        if (n == 0) {
            logInfo(
                "zip: unexpected EOF reading stored entry name={s} remaining={d} want={d}",
                .{ entry_name, remaining, want },
            );
            return Error.Internal;
        }
        try writeAll(out_file, buf[0..n]);
        if (sha_opt) |sha| sha.update(buf[0..n]);
        written += n;
        remaining -= @intCast(n);
    }
    return written;
}

fn extractDeflate(
    allocator: std.mem.Allocator,
    file: *fs.File,
    e: *const Entry,
    entry_name: []const u8,
    out_file: *fs.File,
    sha_opt: ?*std.crypto.hash.sha2.Sha256,
) !u64 {
    logInfo(
        "zip: deflate begin name={s} comp_size={d} uncomp_size={d}",
        .{ entry_name, e.comp_size, e.uncomp_size },
    );

    var in_reader = FileReader.init(file);
    var limited_buf: [256]u8 = undefined;
    var limited = std.Io.Reader.Limited.init(&in_reader.reader, .limited(e.comp_size), limited_buf[0..]);
    var decomp = std.compress.flate.Decompress.init(&limited.interface, .raw, &.{});

    var writer = HashWriter{
        .file = out_file,
        .sha_opt = sha_opt,
    };
    // `std.compress.flate.Decompress` in direct mode writes via `Writer.write*Preserve`,
    // which requires preserving `flate.history_len` bytes plus the largest
    // single match copy (`kDeflateMaxMatchLen`).
    const writer_buf = allocator.alloc(u8, kDeflateWriterBufLen) catch |err| {
        logInfo(
            "zip: deflate writer alloc failed name={s} bytes={d} err={s}",
            .{ entry_name, kDeflateWriterBufLen, @errorName(err) },
        );
        return err;
    };
    defer allocator.free(writer_buf);
    writer.interface.buffer = writer_buf;
    const total = decomp.reader.streamRemaining(&writer.interface) catch |err| {
        const off = file.seek(.{ .Current = 0 }) catch -1;
        const end_off = blk: {
            const cur = off;
            if (cur < 0) break :blk -1;
            const end = file.seek(.{ .End = 0 }) catch break :blk -1;
            _ = file.seek(.{ .Start = cur }) catch {};
            break :blk end;
        };
        const inner = decomp.err;
        logInfo(
            "zip: deflate decompress failed name={s} err={s} inner={s} comp_size={d} uncomp_size={d} wrote={d} file_off={d} file_end={d}",
            .{
                entry_name,
                @errorName(err),
                if (inner) |ie| @errorName(ie) else "none",
                e.comp_size,
                e.uncomp_size,
                writer.written,
                off,
                end_off,
            },
        );
        return Error.Internal;
    };
    writer.interface.flush() catch return Error.Internal;
    _ = limited.interface.discardRemaining() catch {};
    logInfo("zip: deflate done name={s} wrote={d}", .{ entry_name, total });
    return total;
}

pub fn firstPassFindManifest(file: *fs.File, allocator: std.mem.Allocator) ![]u8 {
    _ = try file.seek(.{ .Start = 0 });

    while (true) {
        const e_opt = nextEntry(allocator, file) catch |err| {
            logInfo("zip: nextEntry failed during manifest scan err={s}", .{@errorName(err)});
            return err;
        };
        if (e_opt == null) break;
        var e = e_opt.?;
        defer allocator.free(e.name);

        if (std.mem.eql(u8, e.name, "manifest.json")) {
            logInfo(
                "zip: found manifest.json method={s} flags=0x{x} comp_size={d} uncomp_size={d}",
                .{ @tagName(e.method), e.flags, e.comp_size, e.uncomp_size },
            );
            if (e.method == .store) {
                const out = try allocator.alloc(u8, e.comp_size);
                errdefer allocator.free(out);
                try readExact(file, out);
                return out;
            }

            var outw: std.Io.Writer.Allocating = .init(allocator);
            errdefer outw.deinit();

            var in_reader = FileReader.init(file);
            var limited_buf: [256]u8 = undefined;
            var limited = std.Io.Reader.Limited.init(&in_reader.reader, .limited(e.comp_size), limited_buf[0..]);
            var decomp = std.compress.flate.Decompress.init(&limited.interface, .raw, &.{});
            _ = decomp.reader.streamRemaining(&outw.writer) catch |err| {
                logInfo("zip: manifest.json decompress failed err={s}", .{@errorName(err)});
                return Error.Internal;
            };
            _ = limited.interface.discardRemaining() catch {};
            const out = try outw.toOwnedSlice();
            return out;
        }

        try skipEntryData(file, &e);
    }

    return error.MissingManifest;
}

pub const ExtractResult = struct {
    wasm_sha256: [32]u8,
};

pub fn extractAll(
    allocator: std.mem.Allocator,
    file: *fs.File,
    dest_root_z: [:0]const u8,
) !ExtractResult {
    _ = try file.seek(.{ .Start = 0 });

    logInfo("zip: begin extract dest={s}", .{dest_root_z});

    var saw_wasm = false;
    var sha: std.crypto.hash.sha2.Sha256 = .init(.{});

    while (true) {
        const e_opt = nextEntry(allocator, file) catch |err| {
            logInfo("zip: nextEntry failed during extract err={s}", .{@errorName(err)});
            return err;
        };
        if (e_opt == null) break;
        var e = e_opt.?;
        defer allocator.free(e.name);

        const name = e.name;
        logInfo(
            "zip: entry name={s} method={s} flags=0x{x} comp_size={d} uncomp_size={d}",
            .{ name, @tagName(e.method), e.flags, e.comp_size, e.uncomp_size },
        );
        const is_manifest = std.mem.eql(u8, name, "manifest.json");
        const is_wasm = std.mem.eql(u8, name, "app.wasm");
        const is_icon = std.mem.eql(u8, name, "icon.png");
        const is_asset = std.mem.startsWith(u8, name, "assets/");

        if (!is_manifest and !is_wasm and !is_icon and !is_asset) {
            try skipEntryData(file, &e);
            continue;
        }

        var out_path_buf: [256]u8 = undefined;
        const out_path_z = std.fmt.bufPrintZ(&out_path_buf, "{s}/{s}", .{ dest_root_z, name }) catch return Error.InvalidArgument;
        logInfo("zip: extracting name={s} to={s}", .{ name, out_path_z });

        if (is_asset) {
            // Ensure assets parent directory exists.
            const out_path: []const u8 = out_path_z;
            if (std.mem.lastIndexOfScalar(u8, out_path, '/')) |slash| {
                var dir_buf: [256]u8 = undefined;
                const dir = out_path[0..slash];
                if (dir.len < dir_buf.len) {
                    @memcpy(dir_buf[0..dir.len], dir);
                    dir_buf[dir.len] = 0;
                    mkdirP(dir_buf[0..dir.len :0]);
                }
            }
        }
        if (is_manifest or is_wasm or is_icon) {
            mkdirP(dest_root_z);
        }

        var out_file = fs.File.open(out_path_z, fs.FS_WRITE | fs.FS_CREATE | fs.FS_TRUNC) catch |err| {
            logInfo("zip: open output failed name={s} path={s} err={s}", .{ name, out_path_z, @errorName(err) });
            return err;
        };
        defer out_file.close() catch {};

        const sha_opt: ?*std.crypto.hash.sha2.Sha256 = if (is_wasm) &sha else null;
        const written = switch (e.method) {
            .store => try extractStored(file, &e, name, &out_file, sha_opt),
            .deflate => try extractDeflate(allocator, file, &e, name, &out_file, sha_opt),
        };

        if (written != e.uncomp_size) {
            logInfo(
                "zip: size mismatch name={s} wrote={d} expected_uncomp={d} comp_size={d} method={s}",
                .{ name, written, e.uncomp_size, e.comp_size, @tagName(e.method) },
            );
            return Error.Internal;
        }
        logInfo("zip: extracted ok name={s} bytes={d}", .{ name, written });
        if (is_wasm) saw_wasm = true;
    }

    if (!saw_wasm) return error.MissingWasm;
    var digest: [32]u8 = undefined;
    sha.final(&digest);
    return .{ .wasm_sha256 = digest };
}
