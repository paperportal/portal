#pragma once

#include <stdint.h>

// PaperPortal WASM host contract v1.
// This header is documentation-first and provides constants the host compiles against.

namespace pp_contract {

constexpr int32_t kContractVersion = 1;

// Exported handler names (required).
constexpr const char *kExportContractVersion = "pp_contract_version";
constexpr const char *kExportInit = "pp_init";
constexpr const char *kExportTick = "pp_tick";
constexpr const char *kExportAlloc = "pp_alloc";
constexpr const char *kExportFree = "pp_free";

// pp_init signature:
//   int32_t pp_init(int32_t api_version, int32_t screen_w, int32_t screen_h,
//                  int32_t args_ptr, int32_t args_len)
// Returns 0 on success, negative on failure.
// - args_ptr: pointer to JSON string in wasm memory (null if no args)
// - args_len: length of JSON string (0 if no args)
// The JSON string is NOT null-terminated; use args_len for bounds checking.

// Exported handler names (optional).
constexpr const char *kExportOnGesture = "pp_on_gesture";
constexpr const char *kExportOnHttpRequest = "pp_on_http_request";
constexpr const char *kExportOnWifiEvent = "pp_on_wifi_event";
constexpr const char *kExportShutdown = "pp_shutdown";

// Gesture kinds (pp_on_gesture kind argument).
enum PpGestureKind : int32_t {
    kGestureTap = 1,
    kGestureLongPress = 2,
    kGestureFlick = 3,
    kGestureDragStart = 4,
    kGestureDragMove = 5,
    kGestureDragEnd = 6,
};

// Host-controlled gesture thresholds (v1). Values are in milliseconds or pixels.
constexpr int32_t kTapMaxDurationMs = 250;
constexpr int32_t kTapMaxMovePx = 8;
constexpr int32_t kLongPressMinDurationMs = 500;
constexpr int32_t kFlickMinDistancePx = 24;
constexpr int32_t kFlickMaxDurationMs = 250;

// HTTP flags for pp_on_http_request.
constexpr int32_t kHttpFlagBodyTruncated = 1 << 0;

// Maximum request body bytes to copy into wasm memory for pp_on_http_request.
constexpr int32_t kHttpMaxBodyBytes = 8 * 1024;

// Wi-Fi event kinds (pp_on_wifi_event kind argument).
enum PpWifiEventKind : int32_t {
    kWifiEventStaStart = 1,
    kWifiEventStaDisconnected = 2,
    kWifiEventStaGotIp = 3,
};

} // namespace pp_contract
