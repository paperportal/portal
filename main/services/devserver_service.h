#pragma once

#include <atomic>
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
    std::atomic<uint32_t> refcount{0};
    int32_t result = 0;
    char message[160] = {};

    // Allocates a reply object with 2 owners: the HTTP handler and the host event loop.
    // Each side must call Release() exactly once.
    static DevCommandReply *CreateForDevCommand()
    {
        DevCommandReply *reply = new DevCommandReply();
        reply->refcount.store(2, std::memory_order_relaxed);
        reply->done = xSemaphoreCreateBinary();
        if (!reply->done) {
            delete reply;
            return nullptr;
        }
        return reply;
    }

    void Release()
    {
        if (refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            if (done) {
                vSemaphoreDelete(done);
                done = nullptr;
            }
            delete this;
        }
    }
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
bool is_starting(void);

int get_url(char *out, size_t out_len);
int get_ap_ssid(char *out, size_t out_len);
int get_ap_password(char *out, size_t out_len);
int get_last_error(char *out, size_t out_len);

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
