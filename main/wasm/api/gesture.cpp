#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "wasm_export.h"

#include "input/gesture_engine.h"

#include "../api.h"
#include "errors.h"

namespace {

constexpr const char *kTag = "wasm_api_gesture";

int32_t gestureClearAll(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    gesture_engine().ClearCustom();
    return kWasmOk;
}

int32_t gestureRemove(wasm_exec_env_t exec_env, int32_t handle)
{
    (void)exec_env;
    if (handle <= 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRemove: handle <= 0");
        return kWasmErrInvalidArgument;
    }

    const int32_t rc = gesture_engine().Remove(handle);
    if (rc == 0) {
        return kWasmOk;
    }
    if (rc == kWasmErrNotFound) {
        wasm_api_set_last_error(kWasmErrNotFound, "gestureRemove: handle not found");
        return kWasmErrNotFound;
    }

    wasm_api_set_last_error(kWasmErrInternal, "gestureRemove: remove failed");
    return kWasmErrInternal;
}

int32_t gestureRegisterPolyline(wasm_exec_env_t exec_env, const char *id_z, const uint8_t *points, size_t points_len,
    int32_t fixed, float tolerance_px, int32_t priority, int32_t max_duration_ms, int32_t options)
{
    (void)exec_env;

    if (!id_z) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: id is null");
        return kWasmErrInvalidArgument;
    }
    const size_t id_len = strnlen(id_z, 48);
    if (id_len == 0 || id_len >= 48) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: id must be 1..47 bytes (NUL-terminated)");
        return kWasmErrInvalidArgument;
    }

    if (!points && points_len != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: points is null");
        return kWasmErrInvalidArgument;
    }
    if ((points_len % 8) != 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: points_len must be divisible by 8");
        return kWasmErrInvalidArgument;
    }
    if (points_len < 16) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: expected at least 2 points");
        return kWasmErrInvalidArgument;
    }

    if (!(tolerance_px > 0.0f)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: tolerance_px must be > 0");
        return kWasmErrInvalidArgument;
    }

    if (!(fixed == 0 || fixed == 1)) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: fixed must be 0 or 1");
        return kWasmErrInvalidArgument;
    }

    if (max_duration_ms < 0) {
        wasm_api_set_last_error(kWasmErrInvalidArgument, "gestureRegisterPolyline: max_duration_ms < 0");
        return kWasmErrInvalidArgument;
    }

    // Options bit 0: disable segment constraint when set. Default is enabled.
    const bool segment_constraint_enabled = (options & 0x1) == 0;

    const size_t point_count = points_len / 8;
    std::vector<GestureEngine::PointF> pts;
    pts.reserve(point_count);
    for (size_t i = 0; i < point_count; i++) {
        float x = 0.0f;
        float y = 0.0f;
        memcpy(&x, points + (i * 8) + 0, 4);
        memcpy(&y, points + (i * 8) + 4, 4);
        pts.push_back({ x, y });
    }

    const int32_t handle = gesture_engine().RegisterPolyline(id_z, std::move(pts), fixed != 0, tolerance_px, priority,
        (uint32_t)max_duration_ms, segment_constraint_enabled);
    if (handle <= 0) {
        wasm_api_set_last_error(kWasmErrInternal, "gestureRegisterPolyline: register failed");
        return kWasmErrInternal;
    }

    ESP_LOGI(kTag, "Registered custom polyline gesture '%s' (handle=%" PRIi32 ", points=%u, fixed=%" PRIi32 ", tol=%.1f, pri=%" PRIi32
                    ", max_dur=%" PRIi32 ", seg=%d)",
        id_z, handle, (unsigned)point_count, fixed, tolerance_px, priority, max_duration_ms,
        segment_constraint_enabled ? 1 : 0);

    return handle;
}

/* clang-format off */
#define REG_NATIVE_FUNC(funcName, signature) \
    { #funcName, (void *)funcName, signature, NULL }

static NativeSymbol g_gesture_native_symbols[] = {
    REG_NATIVE_FUNC(gestureClearAll, "()i"),
    REG_NATIVE_FUNC(gestureRegisterPolyline, "($*~ifiii)i"),
    REG_NATIVE_FUNC(gestureRemove, "(i)i"),
};
/* clang-format on */

} // namespace

bool wasm_api_register_gesture(void)
{
    const uint32_t count = sizeof(g_gesture_native_symbols) / sizeof(g_gesture_native_symbols[0]);
    bool ok = wasm_runtime_register_natives("m5_gesture", g_gesture_native_symbols, count);
    if (!ok) {
        ESP_LOGE(kTag, "Failed to register m5_gesture natives (count=%" PRIu32 ")", count);
        wasm_api_set_last_error(kWasmErrInternal, "register_gesture: wasm_runtime_register_natives failed");
    }
    return ok;
}
