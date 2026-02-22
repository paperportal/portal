#include "host/event_loop.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pthread.h"

#include "services/devserver_service.h"
#include "host/events.h"
#include "host/httpd_host.h"
#include "host/microtask_scheduler.h"
#include "input/gesture_engine.h"
#include "input/touch_tracker.h"
#include "m5papers3_display.h"
#include "services/wifi_service.h"
#include "services/power_service.h"
#include "wasm/app_contract.h"
#include "wasm/wasm_controller.h"

namespace {

static constexpr const char *kTag = "host_event_loop";
static constexpr uint32_t kQueueDepth = 16;
static constexpr uint32_t kEventLoopStack = 8 * 1024;
static constexpr int32_t kIdleSleepTimeoutMs = 3 * 60 * 1000;
static constexpr uint32_t kTouchPollIdleMs = 50;
static constexpr uint32_t kTouchPollActiveMs = 20;
static constexpr int kMicroTaskMaxStepsPerWake = 16;

static QueueHandle_t g_event_queue = nullptr;
static pthread_t g_event_thread = {};
static bool g_event_thread_started = false;
static volatile bool g_event_loop_running = false;
static volatile bool g_pending_app_exit = false;
static volatile bool g_pending_app_switch = false;
static char g_pending_app_id[64] = "";
static char g_pending_app_args[256] = "";

static wifi::Subscription g_wifi_sub = {};
static bool g_wifi_subscribed = false;

static int32_t g_system_sleep_gesture_handle = 0;

struct GestureState {
    bool active = false;
    bool dragging = false;
    bool long_press_sent = false;
    int32_t start_x = 0;
    int32_t start_y = 0;
    int32_t start_ms = 0;
    int32_t last_x = 0;
    int32_t last_y = 0;
    int32_t pointer_id = 0;
};

int32_t now_ms()
{
    return (int32_t)(esp_timer_get_time() / 1000);
}

uint32_t now_u32_ms()
{
    return (uint32_t)now_ms();
}

bool time_reached(uint32_t now, uint32_t target)
{
    return (uint32_t)(now - target) < 0x80000000u;
}

uint32_t time_until(uint32_t now, uint32_t target)
{
    if (time_reached(now, target)) {
        return 0;
    }
    return (uint32_t)(target - now);
}

void set_earliest_deadline(uint32_t now, uint32_t *deadline, bool *has_deadline, uint32_t candidate)
{
    if (!deadline || !has_deadline) {
        return;
    }
    if (!*has_deadline) {
        *deadline = candidate;
        *has_deadline = true;
        return;
    }
    const uint32_t current_wait = time_until(now, *deadline);
    const uint32_t candidate_wait = time_until(now, candidate);
    if (candidate_wait < current_wait) {
        *deadline = candidate;
    }
}

void ensure_system_gestures_registered()
{
    if (g_system_sleep_gesture_handle > 0) {
        return;
    }

    std::vector<GestureEngine::PointF> points = {
        { 280.0f, 860.0f },
        { 280.0f, 500.0f },
        { 280.0f, 860.0f },
    };

    // "SLP" system gesture: fixed absolute polyline; high priority; short duration.
    g_system_sleep_gesture_handle = gesture_engine().RegisterPolyline("SLP", std::move(points), true, 100.0f, 10, 1500, true, true);
    if (g_system_sleep_gesture_handle <= 0) {
        ESP_LOGE(kTag, "Failed to register system sleep gesture");
        g_system_sleep_gesture_handle = 0;
    }
}

void clear_custom_gestures()
{
    gesture_engine().ClearCustom();
}

void clear_app_runtime_state()
{
    clear_custom_gestures();
    microtask_scheduler().ClearAll();
}

void wifi_service_event_cb(const wifi::Event &event, void *user_ctx)
{
    (void)user_ctx;

    int32_t kind = 0;
    switch (event.kind) {
        case wifi::EventKind::kStaStart:
            kind = pp_contract::kWifiEventStaStart;
            break;
        case wifi::EventKind::kStaDisconnected:
            kind = pp_contract::kWifiEventStaDisconnected;
            break;
        case wifi::EventKind::kStaGotIp:
            kind = pp_contract::kWifiEventStaGotIp;
            break;
        default:
            return;
    }

    if (!g_event_queue) {
        return;
    }

    HostEvent ev = MakeWifiEvent(event.now_ms, kind, 0, 0);
    (void)xQueueSend(g_event_queue, &ev, 0);
}

int32_t abs_i32(int32_t v)
{
    return v < 0 ? -v : v;
}

bool is_lower_uuid(const char *s)
{
    if (!s) {
        return false;
    }
    if (strlen(s) != 36) {
        return false;
    }
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
        return false;
    }
    for (size_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        const char c = s[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

void finish_dev_command(devserver::DevCommand *cmd, int32_t result, const char *message)
{
    if (!cmd) {
        return;
    }

    devserver::DevCommandReply *reply = cmd->reply;
    cmd->reply = nullptr;
    if (reply) {
        reply->result = result;
        if (message) {
            snprintf(reply->message, sizeof(reply->message), "%s", message);
        }
        else {
            reply->message[0] = '\0';
        }
        if (reply->done) {
            xSemaphoreGive(reply->done);
        }
        reply->Release();
    }

    if (cmd->args) {
        heap_caps_free(cmd->args);
        cmd->args = nullptr;
    }
    if (cmd->wasm_bytes) {
        heap_caps_free(cmd->wasm_bytes);
        cmd->wasm_bytes = nullptr;
        cmd->wasm_len = 0;
    }

    delete cmd;
}

void handle_dev_command(WasmController *wasm, devserver::DevCommand *cmd)
{
    if (!cmd) {
        return;
    }
    if (!wasm) {
        finish_dev_command(cmd, -1, "wasm not ready");
        return;
    }

    auto reload_launcher = [&]() -> bool {
        wasm->UnloadModule();
        clear_app_runtime_state();

        if (!wasm->LoadEntrypoint()) {
            return false;
        }

        char err[256] = {};
        if (!wasm->Instantiate(err, sizeof(err))) {
            return false;
        }
        if (!wasm->CallInit(pp_contract::kContractVersion, 0, 0)) {
            return false;
        }
        return true;
    };

    if (cmd->kind == devserver::DevCommandKind::RunUploadedWasm) {
        if (devserver::uploaded_app_is_running()) {
            devserver::notify_uploaded_stopped();
        }

        wasm->CallShutdown();
        wasm->UnloadModule();
        clear_app_runtime_state();

        char err[256] = {};
        if (!wasm->LoadFromBytes(cmd->wasm_bytes, cmd->wasm_len, cmd->args, err, sizeof(err))) {
            devserver::notify_server_error(err[0] ? err : "load failed");
            (void)reload_launcher();
            finish_dev_command(cmd, -2, err[0] ? err : "load failed");
            return;
        }
        if (!wasm->Instantiate(err, sizeof(err))) {
            devserver::notify_server_error(err[0] ? err : "instantiate failed");
            (void)reload_launcher();
            finish_dev_command(cmd, -2, err[0] ? err : "instantiate failed");
            return;
        }

        if (!wasm->CallInit(pp_contract::kContractVersion, 0, 0)) {
            devserver::notify_server_error("ppInit failed");
            (void)reload_launcher();
            finish_dev_command(cmd, -2, "ppInit failed");
            return;
        }

        devserver::notify_uploaded_started();
        finish_dev_command(cmd, 0, "ok");
        return;
    }

    if (cmd->kind == devserver::DevCommandKind::StopUploadedWasm) {
        if (!devserver::uploaded_app_is_running()) {
            finish_dev_command(cmd, 0, "ok");
            return;
        }

        wasm->CallShutdown();
        if (!reload_launcher()) {
            devserver::notify_server_error("reload launcher failed");
            finish_dev_command(cmd, -2, "reload launcher failed");
            return;
        }

        devserver::notify_uploaded_stopped();
        finish_dev_command(cmd, 0, "ok");
        return;
    }

    finish_dev_command(cmd, -1, "unknown dev command");
}

void dispatch_event(WasmController *wasm, const HostEvent &event)
{
    if (!wasm) {
        return;
    }

    switch (event.type) {
        case HostEventType::Tick:
            // Tick events are reserved for host-internal scheduling only.
            break;
        case HostEventType::Gesture: {
            const HostEventGesture &g = event.data.gesture;
            wasm->CallOnGesture(g.kind, g.x, g.y, g.dx, g.dy, g.duration_ms, event.now_ms, g.flags);
            break;
        }
        case HostEventType::HttpRequest:
            if (wasm && wasm->HasHttpRequestHandler()) {
                HttpdHostRequestInfo info{};
                if (!httpd_host_get_request_info(event.data.http.req_id, &info)) {
                    ESP_LOGW(kTag, "HTTP request info missing (req_id=%" PRId32 ")", event.data.http.req_id);
                    break;
                }

                const char *uri = info.uri ? info.uri : "";
                const int32_t uri_len = (int32_t)strnlen(uri, 512);
                int32_t uri_ptr = 0;
                int32_t uri_alloc_len = 0;
                if (uri_len > 0) {
                    uri_alloc_len = uri_len;
                    uri_ptr = wasm->CallAlloc(uri_alloc_len);
                    if (uri_ptr <= 0 || !wasm->WriteAppMemory(uri_ptr, uri, (uint32_t)uri_alloc_len)) {
                        ESP_LOGW(kTag, "Failed to copy URI into wasm memory (req_id=%" PRId32 ")", info.req_id);
                        if (uri_ptr > 0) {
                            wasm->CallFree(uri_ptr, uri_alloc_len);
                        }
                        break;
                    }
                }

                int32_t flags = 0;
                int32_t content_len = info.content_len > 0 ? info.content_len : 0;
                int32_t body_alloc_len = 0;
                int32_t body_len = 0;
                int32_t body_ptr = 0;

                if (content_len > 0) {
                    body_alloc_len = content_len;
                    if (body_alloc_len > pp_contract::kHttpMaxBodyBytes) {
                        body_alloc_len = pp_contract::kHttpMaxBodyBytes;
                        flags |= pp_contract::kHttpFlagBodyTruncated;
                    }
                    body_len = body_alloc_len;
                    body_ptr = wasm->CallAlloc(body_alloc_len);
                    if (body_ptr <= 0) {
                        ESP_LOGW(kTag, "Failed to allocate body buffer in wasm (req_id=%" PRId32 ")", info.req_id);
                        if (uri_ptr > 0) {
                            wasm->CallFree(uri_ptr, uri_alloc_len);
                        }
                        break;
                    }

                    uint8_t *body_native = (uint8_t *)wasm->GetAppMemory(body_ptr, (uint32_t)body_alloc_len);
                    if (!body_native) {
                        ESP_LOGW(kTag, "Failed to map body buffer in wasm (req_id=%" PRId32 ")", info.req_id);
                        wasm->CallFree(body_ptr, body_alloc_len);
                        if (uri_ptr > 0) {
                            wasm->CallFree(uri_ptr, uri_alloc_len);
                        }
                        break;
                    }

                    int32_t remaining = body_alloc_len;
                    int32_t offset = 0;
                    while (remaining > 0) {
                        int ret = httpd_req_recv(info.req, (char *)(body_native + offset), remaining);
                        if (ret <= 0) {
                            ESP_LOGW(kTag, "Failed to read HTTP body (req_id=%" PRId32 ", ret=%d)", info.req_id, ret);
                            flags |= pp_contract::kHttpFlagBodyTruncated;
                            break;
                        }
                        offset += ret;
                        remaining -= ret;
                    }

                    body_len = offset;

                    int32_t remaining_to_discard = content_len - offset;
                    if (remaining_to_discard > 0) {
                        uint8_t scratch[128];
                        flags |= pp_contract::kHttpFlagBodyTruncated;
                        while (remaining_to_discard > 0) {
                            int to_read = (remaining_to_discard > (int32_t)sizeof(scratch))
                                ? (int32_t)sizeof(scratch)
                                : remaining_to_discard;
                            int ret = httpd_req_recv(info.req, (char *)scratch, to_read);
                            if (ret <= 0) {
                                break;
                            }
                            remaining_to_discard -= ret;
                        }
                    }
                }

                wasm->CallOnHttpRequest(info.req_id, info.method, uri_ptr, uri_len, body_ptr, body_len,
                    content_len, event.now_ms, flags);

                if (body_ptr > 0) {
                    wasm->CallFree(body_ptr, body_alloc_len);
                }
                if (uri_ptr > 0) {
                    wasm->CallFree(uri_ptr, uri_alloc_len);
                }
            }
            break;
        case HostEventType::WifiEvent:
            if (event.data.wifi.kind == pp_contract::kWifiEventStaDisconnected) {
                wifi::Status s{};
                bool ap_running = false;
                if (wifi::get_status(&s) == ESP_OK) {
                    ap_running = (s.mode == wifi::Mode::kAp) || (s.ap == wifi::ApState::kRunning);
                }

                if (devserver::is_running() && !ap_running) {
                    devserver::notify_server_error("wifi disconnected");
                    (void)devserver::stop();
                }
            }
            wasm->CallOnWifiEvent(event.data.wifi.kind, event.now_ms, event.data.wifi.arg0, event.data.wifi.arg1);
            break;
        case HostEventType::DevCommand:
            handle_dev_command(wasm, event.data.dev.cmd);
            break;
        default:
            break;
    }
}

void emit_gesture(WasmController *wasm, int32_t now, int32_t kind, int32_t x, int32_t y, int32_t dx, int32_t dy,
    int32_t duration, int32_t flags)
{
    HostEventGesture g = {
        .kind = kind,
        .x = x,
        .y = y,
        .dx = dx,
        .dy = dy,
        .duration_ms = duration,
        .flags = flags,
    };
    HostEvent ev = MakeGestureEvent(now, g);
    dispatch_event(wasm, ev);
}

bool process_touch(WasmController *wasm, GestureState &state, int32_t now)
{
    if (!paper_display_ensure_init()) {
        return false;
    }

    const bool dispatch_to_wasm = (wasm != nullptr) && wasm->HasGestureHandler();

    TouchTracker &tracker = touch_tracker();
    tracker.update(&paper_display(), (uint32_t)now);

    if (tracker.getCount() == 0) {
        if (!state.active) {
            return false;
        }
    }

    const TouchDetail &det = tracker.getDetail(0);
    const bool pressed = (det.state & touch_state_t::mask_touch) != 0;
    const bool did_input = pressed || state.active;

    if (!state.active && pressed) {
        state.active = true;
        state.dragging = false;
        state.long_press_sent = false;
        state.start_x = det.x;
        state.start_y = det.y;
        state.start_ms = (int32_t)det.base_msec;
        state.last_x = det.x;
        state.last_y = det.y;
        state.pointer_id = det.id;

        gesture_engine().ProcessTouchEvent({
            .type = GestureEngine::TouchType::Down,
            .pointer_id = (int)state.pointer_id,
            .x = (float)det.x,
            .y = (float)det.y,
            .time_ms = (uint64_t)now,
        });
        return did_input;
    }

    if (state.active && pressed) {
        const int32_t dx = det.x - state.start_x;
        const int32_t dy = det.y - state.start_y;
        const int32_t duration = now - state.start_ms;
        const int32_t abs_dx = abs_i32(dx);
        const int32_t abs_dy = abs_i32(dy);

        if (det.x != state.last_x || det.y != state.last_y) {
            gesture_engine().ProcessTouchEvent({
                .type = GestureEngine::TouchType::Move,
                .pointer_id = (int)state.pointer_id,
                .x = (float)det.x,
                .y = (float)det.y,
                .time_ms = (uint64_t)now,
            });
        }

        if (!state.long_press_sent && !state.dragging
            && duration >= pp_contract::kLongPressMinDurationMs
            && abs_dx <= pp_contract::kTapMaxMovePx
            && abs_dy <= pp_contract::kTapMaxMovePx) {
            emit_gesture(wasm, now, pp_contract::kGestureLongPress, det.x, det.y, dx, dy, duration, 0);
            state.long_press_sent = true;
        }

        if (!state.dragging && (abs_dx > pp_contract::kTapMaxMovePx || abs_dy > pp_contract::kTapMaxMovePx)) {
            state.dragging = true;
            emit_gesture(wasm, now, pp_contract::kGestureDragStart, det.x, det.y, dx, dy, duration, 0);
        }

        if (state.dragging && (det.x != state.last_x || det.y != state.last_y)) {
            emit_gesture(wasm, now, pp_contract::kGestureDragMove, det.x, det.y, dx, dy, duration, 0);
        }

        state.last_x = det.x;
        state.last_y = det.y;
        return did_input;
    }

    if (state.active && !pressed) {
        const int32_t dx = state.last_x - state.start_x;
        const int32_t dy = state.last_y - state.start_y;
        const int32_t duration = now - state.start_ms;
        const int32_t abs_dx = abs_i32(dx);
        const int32_t abs_dy = abs_i32(dy);

        const int32_t custom_handle = gesture_engine().ProcessTouchEvent({
            .type = GestureEngine::TouchType::Up,
            .pointer_id = (int)state.pointer_id,
            .x = (float)state.last_x,
            .y = (float)state.last_y,
            .time_ms = (uint64_t)now,
        });

        if (custom_handle > 0 && custom_handle == g_system_sleep_gesture_handle) {
            ESP_LOGI(kTag, "System sleep gesture detected; powering off");
            (void)power_service::power_off(true);
            return did_input;
        }

        if (dispatch_to_wasm && custom_handle > 0) {
            emit_gesture(wasm, now, pp_contract::kGestureCustomPolyline, state.last_x, state.last_y, dx, dy, duration,
                custom_handle);
        }

        if (dispatch_to_wasm && state.dragging) {
            emit_gesture(wasm, now, pp_contract::kGestureDragEnd, state.last_x, state.last_y, dx, dy, duration, 0);
        }
        else if (dispatch_to_wasm && !state.long_press_sent && duration <= pp_contract::kTapMaxDurationMs
            && abs_dx <= pp_contract::kTapMaxMovePx && abs_dy <= pp_contract::kTapMaxMovePx) {
            emit_gesture(wasm, now, pp_contract::kGestureTap, state.last_x, state.last_y, dx, dy, duration, 0);
        }
        else if (dispatch_to_wasm && duration <= pp_contract::kFlickMaxDurationMs
            && (abs_dx >= pp_contract::kFlickMinDistancePx || abs_dy >= pp_contract::kFlickMinDistancePx)) {
            emit_gesture(wasm, now, pp_contract::kGestureFlick, state.last_x, state.last_y, dx, dy, duration, 0);
        }

        state.active = false;
        state.dragging = false;
        state.long_press_sent = false;
        return did_input;
    }

    return did_input;
}

void maybe_recover_uploaded_crash(WasmController *wasm)
{
    if (!wasm) {
        return;
    }
    if (wasm->CanDispatch()) {
        return;
    }
    if (!devserver::uploaded_app_is_running() || !devserver::uploaded_app_is_crashed()) {
        return;
    }

    wasm->UnloadModule();
    clear_app_runtime_state();
    if (!wasm->LoadEntrypoint()) {
        devserver::notify_server_error("crash recovery: reload launcher failed");
        devserver::notify_uploaded_stopped();
        return;
    }

    char err[256] = {};
    if (!wasm->Instantiate(err, sizeof(err))) {
        devserver::notify_server_error("crash recovery: instantiate launcher failed");
        devserver::notify_uploaded_stopped();
        return;
    }

    if (!wasm->CallInit(pp_contract::kContractVersion, 0, 0)) {
        devserver::notify_server_error("crash recovery: launcher ppInit failed");
        devserver::notify_uploaded_stopped();
        return;
    }

    devserver::log_push("uploaded app: crashed; returned to launcher");
    devserver::notify_uploaded_stopped();
}

void host_event_loop_run(WasmController *wasm)
{
    if (!g_event_queue) {
        g_event_queue = xQueueCreate(kQueueDepth, sizeof(HostEvent));
    }
    if (!g_event_queue) {
        ESP_LOGE(kTag, "Failed to create event queue");
        return;
    }

    g_event_loop_running = true;
    GestureState gesture_state;
    MicroTaskScheduler &scheduler = microtask_scheduler();
    scheduler.ClearAll();
    ensure_system_gestures_registered();
    uint32_t last_input_ms = now_u32_ms();
    uint32_t next_touch_poll_ms = last_input_ms;
    bool devserver_active = false;

    while (g_event_loop_running) {
        const uint32_t wait_now = now_u32_ms();
        uint32_t next_deadline = 0;
        bool has_deadline = false;

        set_earliest_deadline(wait_now, &next_deadline, &has_deadline, next_touch_poll_ms);

        if (scheduler.HasTasks()) {
            const uint32_t next_due = scheduler.NextDueMs(wait_now);
            if (next_due != MicroTaskScheduler::kNoDueMs) {
                set_earliest_deadline(wait_now, &next_deadline, &has_deadline, next_due);
            }
        }

        devserver_active = devserver::is_running() || devserver::is_starting();
        if (!devserver_active) {
            const uint32_t idle_deadline = last_input_ms + (uint32_t)kIdleSleepTimeoutMs;
            set_earliest_deadline(wait_now, &next_deadline, &has_deadline, idle_deadline);
        }

        TickType_t wait_ticks = portMAX_DELAY;
        if (has_deadline) {
            const uint32_t wait_ms = time_until(wait_now, next_deadline);
            wait_ticks = (wait_ms == 0) ? 0 : pdMS_TO_TICKS(wait_ms);
            if (wait_ms > 0 && wait_ticks == 0) {
                wait_ticks = 1;
            }
        }

        HostEvent event{};
        if (xQueueReceive(g_event_queue, &event, wait_ticks) == pdTRUE) {
            dispatch_event(wasm, event);
            maybe_recover_uploaded_crash(wasm);
        }

        // App switch takes precedence over app exit if both are requested in one cycle.
        if (g_pending_app_switch && g_pending_app_exit) {
            ESP_LOGI(kTag, "Ignoring pending app exit because app switch is queued");
            g_pending_app_exit = false;
        }

        // Check for pending app switch on every loop iteration (after events are processed)
        if (g_pending_app_switch) {
            ESP_LOGI(kTag, "Processing pending app switch to '%s'", g_pending_app_id);

            auto reload_launcher = [&]() -> bool {
                wasm->UnloadModule();
                clear_app_runtime_state();

                if (!wasm->LoadEmbeddedEntrypoint()) {
                    return false;
                }

                char err[256] = {};
                if (!wasm->Instantiate(err, sizeof(err))) {
                    return false;
                }

                return wasm->CallInit(pp_contract::kContractVersion, 0, 0);
            };

            // Shutdown and unload current app
            if (wasm->IsReady()) {
                wasm->CallShutdown();
            }
            wasm->UnloadModule();
            clear_app_runtime_state();

            // Load the requested app
            bool load_ok = false;
            char load_err[256] = {};
            if (strcmp(g_pending_app_id, "launcher") == 0) {
                load_ok = wasm->LoadEmbeddedEntrypoint();
            } else if (strcmp(g_pending_app_id, "settings") == 0) {
                load_ok = wasm->LoadEmbeddedSettings();
            } else {
                char app_path[256] = {};
                snprintf(app_path, sizeof(app_path), "/sdcard/portal/apps/%s/app.wasm", g_pending_app_id);
                load_ok = wasm->LoadFromFile(app_path, nullptr, load_err, sizeof(load_err));
            }

            if (load_ok) {
                char err[256] = {};
                if (wasm->Instantiate(err, sizeof(err))) {
                    // Parse args if provided
                    int32_t args_ptr = 0;
                    int32_t args_len = 0;
                    if (g_pending_app_args[0] != '\0') {
                        args_len = (int32_t)strlen(g_pending_app_args);
                        args_ptr = wasm->CallAlloc(args_len);
                        if (args_ptr > 0) {
                            wasm->WriteAppMemory(args_ptr, g_pending_app_args, (uint32_t)args_len);
                        }
                    }

                    wasm->CallInit(pp_contract::kContractVersion, args_ptr, args_len);
                    if (args_ptr > 0) {
                        wasm->CallFree(args_ptr, args_len);
                    }

                    ESP_LOGI(kTag, "Successfully switched to app '%s'", g_pending_app_id);
                } else {
                    ESP_LOGE(kTag, "Failed to instantiate app '%s': %s", g_pending_app_id, err);
                    (void)reload_launcher();
                }
            } else {
                if (load_err[0]) {
                    ESP_LOGE(kTag, "Failed to load app '%s': %s", g_pending_app_id, load_err);
                } else {
                    ESP_LOGE(kTag, "Failed to load app '%s'", g_pending_app_id);
                }
                (void)reload_launcher();
            }

            g_pending_app_switch = false;
            g_pending_app_id[0] = '\0';
            g_pending_app_args[0] = '\0';
        } else if (g_pending_app_exit) {
            ESP_LOGI(kTag, "Processing pending app exit");

            // Shutdown and unload current app
            if (wasm->IsReady()) {
                wasm->CallShutdown();
            }
            wasm->UnloadModule();
            clear_app_runtime_state();

            // Relaunch launcher (SD override first, embedded fallback)
            if (!wasm->LoadEntrypoint()) {
                ESP_LOGE(kTag, "Failed to load launcher after app exit");
            } else {
                char err[256] = {};
                if (!wasm->Instantiate(err, sizeof(err))) {
                    ESP_LOGE(kTag, "Failed to instantiate launcher after app exit: %s", err);
                } else {
                    if (!wasm->CallInit(pp_contract::kContractVersion, 0, 0)) {
                        ESP_LOGE(kTag, "Launcher ppInit failed after app exit");
                    } else {
                        ESP_LOGI(kTag, "Returned to launcher after app exit");
                    }
                }
            }

            g_pending_app_exit = false;
        }

        const uint32_t now = now_u32_ms();
        devserver_active = devserver::is_running() || devserver::is_starting();
        if (devserver_active) {
            last_input_ms = now;
        } else {
            const uint32_t idle_deadline = last_input_ms + (uint32_t)kIdleSleepTimeoutMs;
            if (time_reached(now, idle_deadline)) {
                const uint32_t idle_ms = now - last_input_ms;
                ESP_LOGI(kTag, "Idle timeout elapsed; powering off (idle_ms=%" PRIu32 ")", idle_ms);
                (void)power_service::power_off(true);
                last_input_ms = now;
            }
        }

        if (time_reached(now, next_touch_poll_ms)) {
            if (process_touch(wasm, gesture_state, (int32_t)now)) {
                last_input_ms = now;
            }
            const uint32_t poll_interval_ms = gesture_state.active ? kTouchPollActiveMs : kTouchPollIdleMs;
            next_touch_poll_ms = now + poll_interval_ms;
        }

        if (scheduler.HasDue(now)) {
            scheduler.RunDue(wasm, now, kMicroTaskMaxStepsPerWake);
            maybe_recover_uploaded_crash(wasm);
        }
    }
}

void *event_loop_thread(void *arg)
{
    WasmController *wasm = static_cast<WasmController *>(arg);

    if (!wasm_runtime_thread_env_inited()) {
        if (!wasm_runtime_init_thread_env()) {
            ESP_LOGE(kTag, "Failed to init WAMR thread environment");
            return nullptr;
        }
    }

    if (!wasm->Init()) {
        ESP_LOGE(kTag, "Failed to initialize WAMR runtime");
        return nullptr;
    }
    if (!wasm->LoadEntrypoint()) {
        ESP_LOGE(kTag, "Failed to load wasm launcher or entrypoint");
        return nullptr;
    }
    if (!wasm->Instantiate()) {
        ESP_LOGE(kTag, "Failed to instantiate wasm module");
        return nullptr;
    }

    if (!wasm->CallInit(pp_contract::kContractVersion, 0, 0)) {
        ESP_LOGE(kTag, "ppInit failed; continuing without wasm dispatch");
    }

    host_event_loop_run(wasm);
    return nullptr;
}

} // namespace

bool host_event_loop_enqueue(const HostEvent &event, TickType_t timeout_ticks)
{
    if (!g_event_queue) {
        return false;
    }
    return xQueueSend(g_event_queue, &event, timeout_ticks) == pdTRUE;
}

bool host_event_loop_start(WasmController *wasm)
{
    if (g_event_thread_started) {
        return true;
    }

    if (!g_event_queue) {
        g_event_queue = xQueueCreate(kQueueDepth, sizeof(HostEvent));
    }
    if (!g_event_queue) {
        ESP_LOGE(kTag, "Failed to create event queue");
        return false;
    }

    if (!g_wifi_subscribed) {
        wifi::Subscription sub{};
        esp_err_t err = wifi::subscribe(&wifi_service_event_cb, nullptr, &sub);
        if (err == ESP_OK) {
            g_wifi_sub = sub;
            g_wifi_subscribed = true;
        }
        else {
            ESP_LOGW(kTag, "wifi subscribe failed (%d)", (int)err);
        }
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (pthread_attr_setstacksize(&attr, kEventLoopStack) != 0) {
        ESP_LOGE(kTag, "Invalid pthread stack size %u", (unsigned)kEventLoopStack);
        pthread_attr_destroy(&attr);
        return false;
    }

    pthread_t thread;
    int res = pthread_create(&thread, &attr, event_loop_thread, wasm);
    pthread_attr_destroy(&attr);
    if (res != 0) {
        ESP_LOGE(kTag, "Failed to start event loop pthread (%d)", res);
        return false;
    }

    pthread_detach(thread);
    g_event_thread = thread;
    g_event_thread_started = true;
    return true;
}

void host_event_loop_stop()
{
    if (!g_event_thread_started) {
        return;
    }

    ESP_LOGI(kTag, "Stopping event loop...");
    g_event_loop_running = false;

    // Wait a bit for the thread to exit naturally
    // Since we pthread_detach'd, we can't pthread_join, but the thread
    // will exit on its own when g_event_loop_running is false
    vTaskDelay(pdMS_TO_TICKS(100));

    g_event_thread_started = false;
    ESP_LOGI(kTag, "Event loop stopped");
}

void host_event_loop_restart(WasmController *wasm)
{
    ESP_LOGI(kTag, "Restarting event loop...");
    host_event_loop_start(wasm);
    ESP_LOGI(kTag, "Event loop restarted");
}

bool host_event_loop_request_app_exit()
{
    if (!g_event_thread_started) {
        ESP_LOGE(kTag, "request_app_exit: event loop is not running");
        return false;
    }

    ESP_LOGI(kTag, "Requesting app exit");
    g_pending_app_exit = true;
    return true;
}

bool host_event_loop_request_app_switch(const char *app_id, const char *arguments)
{
    if (!app_id) {
        ESP_LOGE(kTag, "request_app_switch: app_id is null");
        return false;
    }

    if (strcmp(app_id, "launcher") != 0 && strcmp(app_id, "settings") != 0 && !is_lower_uuid(app_id)) {
        ESP_LOGE(kTag, "request_app_switch: unknown app_id '%s'", app_id);
        return false;
    }

    ESP_LOGI(kTag, "Requesting app switch to '%s'", app_id);
    snprintf(g_pending_app_id, sizeof(g_pending_app_id), "%s", app_id);
    if (arguments) {
        snprintf(g_pending_app_args, sizeof(g_pending_app_args), "%s", arguments);
    } else {
        g_pending_app_args[0] = '\0';
    }
    g_pending_app_switch = true;
    return true;
}
