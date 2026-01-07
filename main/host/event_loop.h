#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "host/events.h"

class WasmController;

bool host_event_loop_start(WasmController *wasm);
bool host_event_loop_enqueue(const HostEvent &event, TickType_t timeout_ticks = 0);
void host_event_loop_stop();
void host_event_loop_restart(WasmController *wasm);
bool host_event_loop_request_app_exit(void);
bool host_event_loop_request_app_switch(const char *app_id, const char *arguments);
