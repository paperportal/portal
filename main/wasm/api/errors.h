#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum wasm_error_code {
    kWasmOk = 0,
    kWasmErrInvalidArgument = -1,
    kWasmErrInternal = -2,
    kWasmErrNotReady = -3,
    kWasmErrNotFound = -4,
};

void wasm_api_clear_last_error(void);
void wasm_api_set_last_error(int32_t code, const char *message);

int32_t wasm_api_get_last_error_code(void);
const char *wasm_api_get_last_error_message(void);

#ifdef __cplusplus
} // extern "C"
#endif
