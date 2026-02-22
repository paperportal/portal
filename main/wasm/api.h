#pragma once

#include <stdbool.h>

bool wasm_api_register_core(void);
bool wasm_api_register_log(void);
bool wasm_api_register_m5(void);
bool wasm_api_register_microtask(void);
bool wasm_api_register_display(void);
bool wasm_api_register_display_primitives(void);
bool wasm_api_register_display_text(void);
bool wasm_api_register_display_images(void);
bool wasm_api_register_touch(void);
bool wasm_api_register_gesture(void);
bool wasm_api_register_speaker(void);
bool wasm_api_register_rtc(void);
bool wasm_api_register_power(void);
bool wasm_api_register_imu(void);
bool wasm_api_register_net(void);
bool wasm_api_register_http(void);
bool wasm_api_register_httpd(void);
bool wasm_api_register_devserver(void);
bool wasm_api_register_socket(void);
bool wasm_api_register_fs(void);
bool wasm_api_register_nvs(void);
bool wasm_api_register_hal(void);
bool wasm_api_register_all(void);
