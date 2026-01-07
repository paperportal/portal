const std = @import("std");

const manifest_mod = @import("manifest.zig");

pub const SigningError = error{
    MissingSignature,
    InvalidBase64,
    WrongKeyLength,
    WrongSigLength,
    PinnedKeyMismatch,
    SignatureInvalid,
};

fn decodeFixed(comptime N: usize, b64: []const u8) SigningError![N]u8 {
    const dec = std.base64.standard.Decoder;
    const want = dec.calcSizeForSlice(b64) catch return error.InvalidBase64;
    if (want != N) return if (N == 32) error.WrongKeyLength else error.WrongSigLength;
    var out: [N]u8 = undefined;
    dec.decode(out[0..], b64) catch return error.InvalidBase64;
    return out;
}

fn message(allocator: std.mem.Allocator, id: []const u8, checksum: []const u8) ![]u8 {
    return std.fmt.allocPrint(allocator, "paperportal.papp.v1\n{s}\n{s}\n", .{ id, checksum });
}

pub fn verifyAndMaybePin(
    allocator: std.mem.Allocator,
    m: *const manifest_mod.Manifest,
    pinned_publisher_pubkey_b64: ?[]const u8,
) (SigningError || error{OutOfMemory})!?[]u8 {
    const has_sig = (m.publisher_pubkey_b64 != null) and (m.signature_b64 != null);
    if (pinned_publisher_pubkey_b64 != null and !has_sig) {
        return error.MissingSignature;
    }

    if (!has_sig) {
        return null;
    }

    const pk_bytes = try decodeFixed(32, m.publisher_pubkey_b64.?);
    const sig_bytes = try decodeFixed(64, m.signature_b64.?);

    if (pinned_publisher_pubkey_b64) |pinned_b64| {
        const pinned_bytes = try decodeFixed(32, pinned_b64);
        if (!std.mem.eql(u8, pk_bytes[0..], pinned_bytes[0..])) {
            return error.PinnedKeyMismatch;
        }
    }

    const Ed25519 = std.crypto.sign.Ed25519;
    const pk = Ed25519.PublicKey.fromBytes(pk_bytes) catch return error.SignatureInvalid;
    const sig = Ed25519.Signature.fromBytes(sig_bytes);

    const msg = try message(allocator, m.id, m.checksum);
    defer allocator.free(msg);

    sig.verify(msg, pk) catch return error.SignatureInvalid;

    if (pinned_publisher_pubkey_b64 == null) {
        const enc = std.base64.standard.Encoder;
        const out_len = enc.calcSize(pk_bytes.len);
        const out = try allocator.alloc(u8, out_len);
        _ = enc.encode(out, &pk_bytes);
        return out;
    }

    return null;
}
