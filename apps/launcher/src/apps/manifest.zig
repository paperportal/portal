const std = @import("std");

const sdk = @import("paper_portal_sdk");
const Error = sdk.errors.Error;

pub const ManifestError = error{
    NotJsonObject,
    MissingField,
    InvalidField,
    UnsupportedVersion,
    InvalidId,
    InvalidVersionString,
    InvalidChecksum,
    InvalidSigningFields,
    InvalidBase64,
};

pub const Manifest = struct {
    manifest_version: u32,
    sdk_version: u32,
    id: []u8,
    name: []u8,
    version: []u8,
    checksum: []u8,

    publisher_pubkey_b64: ?[]u8 = null,
    signature_b64: ?[]u8 = null,

    raw_json: []u8,

    pub fn deinit(self: *Manifest, allocator: std.mem.Allocator) void {
        allocator.free(self.id);
        allocator.free(self.name);
        allocator.free(self.version);
        allocator.free(self.checksum);
        if (self.publisher_pubkey_b64) |s| allocator.free(s);
        if (self.signature_b64) |s| allocator.free(s);
        allocator.free(self.raw_json);
        self.* = undefined;
    }

    pub fn disownRawJson(self: *Manifest) []u8 {
        const out = self.raw_json;
        self.raw_json = &[_]u8{};
        return out;
    }
};

pub fn validateId(id: []const u8) ManifestError!void {
    if (id.len != 36) return error.InvalidId;
    inline for (.{ 8, 13, 18, 23 }) |dash_idx| {
        if (id[dash_idx] != '-') return error.InvalidId;
    }
    for (id, 0..) |c, i| {
        if (i == 8 or i == 13 or i == 18 or i == 23) continue;
        const is_hex_lower = (c >= '0' and c <= '9') or (c >= 'a' and c <= 'f');
        if (!is_hex_lower) return error.InvalidId;
    }
}

pub fn validateVersionString(version: []const u8) ManifestError!void {
    if (version.len == 0) return error.InvalidVersionString;
    var i: usize = 0;
    var parts: u2 = 0;
    while (true) {
        const start = i;
        while (i < version.len and std.ascii.isDigit(version[i])) : (i += 1) {}
        if (i == start) return error.InvalidVersionString;
        parts += 1;
        if (parts == 3) break;
        if (i >= version.len or version[i] != '.') return error.InvalidVersionString;
        i += 1;
    }
    if (i != version.len) return error.InvalidVersionString;
}

pub fn validateChecksum(checksum: []const u8) ManifestError!void {
    if (!std.mem.startsWith(u8, checksum, "sha256:") or checksum.len != 7 + 64) return error.InvalidChecksum;
    for (checksum[7..]) |c| {
        const ok = (c >= '0' and c <= '9') or (c >= 'a' and c <= 'f');
        if (!ok) return error.InvalidChecksum;
    }
}

fn getString(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    const v = obj.get(key) orelse return null;
    return switch (v) {
        .string => |s| s,
        else => null,
    };
}

fn getInt(obj: std.json.ObjectMap, key: []const u8) ?i64 {
    const v = obj.get(key) orelse return null;
    return switch (v) {
        .integer => |i| i,
        else => null,
    };
}

fn validateB64LenExact(b64: []const u8, want_len: usize) ManifestError!void {
    const dec = std.base64.standard.Decoder;
    const out_len = dec.calcSizeForSlice(b64) catch return error.InvalidBase64;
    if (out_len != want_len) return error.InvalidBase64;
    var tmp: [96]u8 = undefined;
    const buf = tmp[0..want_len];
    dec.decode(buf, b64) catch return error.InvalidBase64;
}

pub fn parse(allocator: std.mem.Allocator, bytes: []u8) (ManifestError || error{OutOfMemory} || Error)!Manifest {
    const parsed = std.json.parseFromSlice(std.json.Value, allocator, bytes, .{}) catch |err| switch (err) {
        error.OutOfMemory => return error.OutOfMemory,
        else => return error.InvalidField,
    };
    defer parsed.deinit();

    if (parsed.value != .object) return error.NotJsonObject;
    const obj = parsed.value.object;

    const mv_i = getInt(obj, "manifest_version") orelse return error.MissingField;
    const sv_i = getInt(obj, "sdk_version") orelse return error.MissingField;
    if (mv_i != 1 or sv_i != 1) return error.UnsupportedVersion;

    const id_s = getString(obj, "id") orelse return error.MissingField;
    const name_s = getString(obj, "name") orelse return error.MissingField;
    const version_s = getString(obj, "version") orelse return error.MissingField;
    const checksum_s = getString(obj, "checksum") orelse return error.MissingField;

    try validateId(id_s);
    try validateVersionString(version_s);
    try validateChecksum(checksum_s);

    const pubkey_s = getString(obj, "publisher_pubkey");
    const sig_s = getString(obj, "signature");
    if ((pubkey_s == null) != (sig_s == null)) return error.InvalidSigningFields;
    if (pubkey_s) |pk| try validateB64LenExact(pk, 32);
    if (sig_s) |sig| try validateB64LenExact(sig, 64);

    return .{
        .manifest_version = 1,
        .sdk_version = 1,
        .id = try allocator.dupe(u8, id_s),
        .name = try allocator.dupe(u8, name_s),
        .version = try allocator.dupe(u8, version_s),
        .checksum = try allocator.dupe(u8, checksum_s),
        .publisher_pubkey_b64 = if (pubkey_s) |s| try allocator.dupe(u8, s) else null,
        .signature_b64 = if (sig_s) |s| try allocator.dupe(u8, s) else null,
        .raw_json = bytes,
    };
}
