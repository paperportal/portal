#include "wasm_controller.h"

namespace {
    static WasmController *g_wasm_controller = nullptr;
}

void wasm_api_set_controller(WasmController *controller)
{
    g_wasm_controller = controller;
}

WasmController *wasm_api_get_controller(void)
{
    return g_wasm_controller;
}
