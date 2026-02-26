#pragma once

#include <stdint.h>

// Bitset returned by `m5::apiFeatures()`.
// Keep this mapping stable; new capabilities should take new bits.
enum wasm_api_feature_bits : uint64_t {
    kWasmFeatureCore = 1ULL << 0, // Category 0: Infrastructure
    kWasmFeatureM5 = 1ULL << 1,   // Category 1: M5Unified core + timing + logging
    kWasmFeatureDisplayBasics = 1ULL << 2, // Category 2: M5GFX display basics
    kWasmFeatureDisplayPrimitives = 1ULL << 3, // Category 3: M5GFX drawing primitives
    kWasmFeatureDisplayText = 1ULL << 4, // Category 4: M5GFX text
    kWasmFeatureDisplayImages = 1ULL << 5, // Category 5: M5GFX bulk pixel ops / images
    kWasmFeatureTouch = 1ULL << 6, // Category 6: M5Unified touch
    kWasmFeatureFastEPD = 1ULL << 7, // Category 8: FastEPD e-ink display library
    kWasmFeatureSpeaker = 1ULL << 8, // Category 7: M5Unified speaker
    kWasmFeatureRTC = 1ULL << 9, // Category 9: M5Unified RTC
    kWasmFeaturePower = 1ULL << 10, // Category 10: M5Unified Power
    kWasmFeatureIMU = 1ULL << 11, // Category 11: M5Unified IMU
    kWasmFeatureNet = 1ULL << 12, // Category 11a: Wi-Fi station mode
    kWasmFeatureHttp = 1ULL << 13, // Category 11b: HTTP client
    kWasmFeatureHttpd = 1ULL << 14, // Category 11c: HTTP server
    kWasmFeatureSocket = 1ULL << 15, // Category 11d: BSD sockets
    kWasmFeatureFS = 1ULL << 16, // Category 12: SD card filesystem
    kWasmFeatureNVS = 1ULL << 17, // Category 13: NVS key-value storage
    kWasmFeatureDevServer = 1ULL << 18, // Category 14: Developer mode devserver control
    kWasmFeatureDisplayMode = 1ULL << 19, // Category 2a: Display color depth / mode selection
    kWasmFeatureSocketTls = 1ULL << 20, // Category 11e: TLS-protected sockets
};
