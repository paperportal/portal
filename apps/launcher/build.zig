const std = @import("std");
const sdk = @import("paper_portal_sdk");

pub fn build(b: *std.Build) void {
    const app = sdk.addPortalApp(b, .{
        .export_symbol_names = &.{
            "pp_contract_version",
            "pp_init",
            "pp_tick",
            "pp_shutdown",
            "pp_alloc",
            "pp_free",
            "pp_on_gesture",
        },
    });

    const install_step = b.addInstallFile(app.exe.getEmittedBin(), "../../../main/assets/entrypoint.wasm");
    b.getInstallStep().dependOn(&install_step.step);
}
