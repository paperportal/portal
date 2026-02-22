#pragma once

#include <stdint.h>

// PaperPortal WASM host contract v1.
// This header is documentation-first and provides constants the host compiles against.

namespace pp_contract {

constexpr int32_t kContractVersion = 1;

// Exported handler names (required).
constexpr const char *kExportContractVersion = "portalContractVersion";
constexpr const char *kExportInit = "ppInit";
constexpr const char *kExportPortalMicroTaskStep = "portalMicroTaskStep";
constexpr const char *kExportAlloc = "portalAlloc";
constexpr const char *kExportFree = "portalFree";

// ppInit signature:
//   int32_t ppInit(int32_t api_version, int32_t args_ptr, int32_t args_len)
// Returns 0 on success, negative on failure.
// - args_ptr: pointer to JSON string in wasm memory (null if no args)
// - args_len: length of JSON string (0 if no args)
// The JSON string is NOT null-terminated; use args_len for bounds checking.

// Exported handler names (optional).
constexpr const char *kExportTick = "ppTick";
constexpr const char *kExportOnGesture = "ppOnGesture";
constexpr const char *kExportOnHttpRequest = "ppOnHttpRequest";
constexpr const char *kExportOnWifiEvent = "ppOnWifiEvent";
constexpr const char *kExportShutdown = "ppShutdown";

// portalMicroTaskStep signature:
//   int64_t portalMicroTaskStep(int32_t handle, int32_t now_ms)
//
// Return encoding:
//   high 32 bits: action kind
//   low  32 bits: action argument (milliseconds for SLEEP_MS; otherwise 0)
enum PpMicroTaskActionKind : uint32_t {
    kMicroTaskActionDone = 0,
    kMicroTaskActionYield = 1,
    kMicroTaskActionSleepMs = 2,
};

// Gesture kinds (ppOnGesture kind argument).
enum PpGestureKind : int32_t {
    kGestureTap = 1,
    kGestureLongPress = 2,
    kGestureFlick = 3,
    kGestureDragStart = 4,
    kGestureDragMove = 5,
    kGestureDragEnd = 6,
    // Custom polyline gesture recognition (registered by the app via `m5_gesture`).
    //
    // For this kind:
    // - `flags` is the winning gesture handle returned by `gestureRegisterPolyline`.
    // - `x,y` are the Up coordinates.
    // - `dx,dy` are Up minus Down.
    // - `duration_ms` is the touch duration.
    kGestureCustomPolyline = 100,
};

// Host-controlled gesture thresholds (v1). Values are in milliseconds or pixels.
constexpr int32_t kTapMaxDurationMs = 250;
constexpr int32_t kTapMaxMovePx = 8;
constexpr int32_t kLongPressMinDurationMs = 500;
constexpr int32_t kFlickMinDistancePx = 24;
constexpr int32_t kFlickMaxDurationMs = 250;

// HTTP flags for ppOnHttpRequest.
constexpr int32_t kHttpFlagBodyTruncated = 1 << 0;

// Maximum request body bytes to copy into wasm memory for ppOnHttpRequest.
constexpr int32_t kHttpMaxBodyBytes = 8 * 1024;

// Wi-Fi event kinds (ppOnWifiEvent kind argument).
enum PpWifiEventKind : int32_t {
    kWifiEventStaStart = 1,
    kWifiEventStaDisconnected = 2,
    kWifiEventStaGotIp = 3,
};

} // namespace pp_contract
