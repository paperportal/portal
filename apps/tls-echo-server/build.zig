const std = @import("std");
const sdk = @import("paper_portal_sdk_local");

pub fn build(b: *std.Build) void {
    _ = sdk.addPortalApp(b, .{
        .local_sdk_path = "../../../zig-sdk",
        .export_symbol_names = &.{"ppShutdown"},
        .exe_name = "tls_echo_server",
    });
}
