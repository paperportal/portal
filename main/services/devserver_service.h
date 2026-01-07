#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace devserver {

enum class DevCommandKind : int32_t {
    RunUploadedWasm = 1,
    StopUploadedWasm = 2,
};

struct DevCommandReply {
    SemaphoreHandle_t done = nullptr;
    int32_t result = 0;
    char message[160] = {};
};

struct DevCommand {
    DevCommandKind kind = DevCommandKind::RunUploadedWasm;

    uint8_t *wasm_bytes = nullptr;
    size_t wasm_len = 0;
    char *args = nullptr;

    DevCommandReply *reply = nullptr;
};

esp_err_t start(void);
esp_err_t stop(void);
bool is_running(void);

int get_url(char *out, size_t out_len);
int get_ap_ssid(char *out, size_t out_len);
int get_ap_password(char *out, size_t out_len);

void log_push(const char *line);
void log_pushf(const char *fmt, ...);

void notify_uploaded_started(void);
void notify_uploaded_stopped(void);
void notify_uploaded_crashed(const char *reason);

void notify_server_error(const char *reason);

bool uploaded_app_is_running(void);
bool uploaded_app_is_crashed(void);
int get_last_crash_reason(char *out, size_t out_len);

} // namespace devserver
