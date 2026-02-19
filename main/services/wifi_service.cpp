#include "services/wifi_service.h"
#include <string.h>
#include <mutex>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"

namespace wifi {

namespace {

constexpr const char *kTag = "wifi_service";

constexpr int kMaxSubscribers = 8;

constexpr EventBits_t kBitStaConnected = BIT0;
constexpr EventBits_t kBitStaGotIp = BIT1;
constexpr EventBits_t kBitStaFailed = BIT2;

struct Subscriber {
    bool in_use;
    int32_t id;
    EventCallback cb;
    void *user_ctx;
};

static std::mutex g_mutex;
static bool g_initialized = false;
static bool g_handlers_registered = false;
static int32_t g_next_sub_id = 1;
static Subscriber g_subs[kMaxSubscribers] = {};

static esp_netif_t *g_netif_sta = nullptr;
static esp_netif_t *g_netif_ap = nullptr;

static char g_hostname[64] = {};

static bool g_mdns_started = false;
static bool g_mdns_starting = false;

static StaState g_sta_state = StaState::kStopped;
static ApState g_ap_state = ApState::kStopped;

static wifi_err_reason_t g_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;

static EventGroupHandle_t g_sta_wait_group = nullptr;

static bool g_connect_in_progress = false;
static int32_t g_connect_retry_num = 0;
static int32_t g_connect_max_retries = 0;

int32_t now_ms()
{
    return (int32_t)(esp_timer_get_time() / 1000);
}

bool str_is_empty(const char *s)
{
    return !s || s[0] == '\0';
}

Mode mode_from_wifi_mode(wifi_mode_t mode)
{
    switch (mode) {
        case WIFI_MODE_STA:
            return Mode::kSta;
        case WIFI_MODE_AP:
            return Mode::kAp;
        default:
            return Mode::kOff;
    }
}

esp_err_t ensure_sta_netif_locked()
{
    if (g_netif_sta) {
        return ESP_OK;
    }

    g_netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!g_netif_sta) {
        g_netif_sta = esp_netif_create_default_wifi_sta();
        if (!g_netif_sta) {
            ESP_LOGE(kTag, "esp_netif_create_default_wifi_sta failed");
            return ESP_FAIL;
        }
    }

    if (g_hostname[0] != '\0') {
        (void)esp_netif_set_hostname(g_netif_sta, g_hostname);
    }

    return ESP_OK;
}

esp_err_t ensure_ap_netif_locked()
{
    if (g_netif_ap) {
        return ESP_OK;
    }

    g_netif_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!g_netif_ap) {
        g_netif_ap = esp_netif_create_default_wifi_ap();
        if (!g_netif_ap) {
            ESP_LOGE(kTag, "esp_netif_create_default_wifi_ap failed");
            return ESP_FAIL;
        }
    }

    if (g_hostname[0] != '\0') {
        (void)esp_netif_set_hostname(g_netif_ap, g_hostname);
    }

    return ESP_OK;
}

esp_err_t wifi_start_locked()
{
    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (err == ESP_ERR_WIFI_NOT_STOPPED) {
        return ESP_OK;
    }
    ESP_LOGE(kTag, "esp_wifi_start failed (%s)", esp_err_to_name(err));
    return err;
}

esp_err_t wifi_stop_locked()
{
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return ESP_OK;
    }
    ESP_LOGE(kTag, "esp_wifi_stop failed (%s)", esp_err_to_name(err));
    return err;
}

esp_err_t set_mode_exclusive_locked(wifi_mode_t desired)
{
    wifi_mode_t current = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&current);
    if (err != ESP_OK) {
        current = WIFI_MODE_NULL;
    }

    if (current != WIFI_MODE_NULL && current != desired) {
        (void)wifi_stop_locked();
    }

    err = esp_wifi_set_mode(desired);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_mode failed (%s)", esp_err_to_name(err));
        return err;
    }

    return wifi_start_locked();
}

esp_err_t set_mode_exclusive_no_start_locked(wifi_mode_t desired)
{
    wifi_mode_t current = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&current);
    if (err != ESP_OK) {
        current = WIFI_MODE_NULL;
    }

    if (current != WIFI_MODE_NULL && current != desired) {
        (void)wifi_stop_locked();
    }

    err = esp_wifi_set_mode(desired);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_mode failed (%s)", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

void dispatch_event_to_subscribers(const Event &event)
{
    struct LocalSub {
        EventCallback cb;
        void *user_ctx;
    };

    LocalSub locals[kMaxSubscribers];
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int i = 0; i < kMaxSubscribers; i++) {
            if (!g_subs[i].in_use || !g_subs[i].cb) {
                continue;
            }
            locals[count++] = { g_subs[i].cb, g_subs[i].user_ctx };
        }
    }

    for (int i = 0; i < count; i++) {
        locals[i].cb(event, locals[i].user_ctx);
    }
}

void signal_sta_connected()
{
    if (g_sta_wait_group) {
        xEventGroupSetBits(g_sta_wait_group, kBitStaConnected);
    }
}

void signal_sta_got_ip()
{
    if (g_sta_wait_group) {
        xEventGroupSetBits(g_sta_wait_group, kBitStaConnected | kBitStaGotIp);
    }
}

void signal_sta_failed()
{
    if (g_sta_wait_group) {
        xEventGroupSetBits(g_sta_wait_group, kBitStaFailed);
    }
}

bool should_retry_connect_locked()
{
    if (!g_connect_in_progress) {
        return false;
    }
    if (g_connect_max_retries < 0) {
        return true;
    }
    return g_connect_retry_num < g_connect_max_retries;
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    Event ev{};
    bool emit = false;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (g_sta_state == StaState::kStopped) {
                        g_sta_state = StaState::kDisconnected;
                    }
                }
                ev.kind = EventKind::kStaStart;
                ev.now_ms = now_ms();
                emit = true;
                break;
            }
            case WIFI_EVENT_STA_CONNECTED: {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_sta_state = StaState::kConnectedNoIp;
                }
                signal_sta_connected();
                ev.kind = EventKind::kStaConnected;
                ev.now_ms = now_ms();
                emit = true;
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_err_reason_t reason = WIFI_REASON_UNSPECIFIED;
                auto *disc = static_cast<wifi_event_sta_disconnected_t *>(event_data);
                if (disc) {
                    reason = (wifi_err_reason_t)disc->reason;
                }

                bool do_retry = false;
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_sta_state = StaState::kDisconnected;
                    g_last_disconnect_reason = reason;

                    if (should_retry_connect_locked()) {
                        g_connect_retry_num++;
                        do_retry = true;
                    }
                    else {
                        g_connect_in_progress = false;
                    }
                }

                if (do_retry) {
                    esp_err_t err = esp_wifi_connect();
                    if (err != ESP_OK) {
                        ESP_LOGW(kTag, "retry esp_wifi_connect failed (%s)", esp_err_to_name(err));
                        {
                            std::lock_guard<std::mutex> lock(g_mutex);
                            g_connect_in_progress = false;
                        }
                        signal_sta_failed();
                    }
                }
                else {
                    signal_sta_failed();
                }

                ev.kind = EventKind::kStaDisconnected;
                ev.now_ms = now_ms();
                ev.data.sta_disconnected.reason = reason;
                emit = true;
                break;
            }
            case WIFI_EVENT_AP_START: {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_ap_state = ApState::kRunning;
                }
                ev.kind = EventKind::kApStart;
                ev.now_ms = now_ms();
                emit = true;
                break;
            }
            case WIFI_EVENT_AP_STOP: {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_ap_state = ApState::kStopped;
                }
                ev.kind = EventKind::kApStop;
                ev.now_ms = now_ms();
                emit = true;
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED: {
                auto *info = static_cast<wifi_event_ap_staconnected_t *>(event_data);
                ev.kind = EventKind::kApStaConnected;
                ev.now_ms = now_ms();
                if (info) {
                    memcpy(ev.data.ap_sta.mac, info->mac, sizeof(ev.data.ap_sta.mac));
                    ev.data.ap_sta.aid = info->aid;
                }
                emit = true;
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                auto *info = static_cast<wifi_event_ap_stadisconnected_t *>(event_data);
                ev.kind = EventKind::kApStaDisconnected;
                ev.now_ms = now_ms();
                if (info) {
                    memcpy(ev.data.ap_sta.mac, info->mac, sizeof(ev.data.ap_sta.mac));
                    ev.data.ap_sta.aid = info->aid;
                }
                emit = true;
                break;
            }
            default:
                break;
        }
    }
    else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t local = {};
                auto *got = static_cast<ip_event_got_ip_t *>(event_data);
                if (got) {
                    local = *got;
                }

                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_sta_state = StaState::kConnectedHasIp;
                    g_connect_in_progress = false;
                }
                signal_sta_got_ip();

                ev.kind = EventKind::kStaGotIp;
                ev.now_ms = now_ms();
                ev.data.sta_got_ip.ip = local.ip_info;
                emit = true;
                break;
            }
            case IP_EVENT_STA_LOST_IP: {
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (g_sta_state == StaState::kConnectedHasIp) {
                        g_sta_state = StaState::kConnectedNoIp;
                    }
                }
                ev.kind = EventKind::kStaLostIp;
                ev.now_ms = now_ms();
                emit = true;
                break;
            }
            default:
                break;
        }
    }

    if (emit) {
        dispatch_event_to_subscribers(ev);
    }
}

esp_err_t clear_sta_wait_bits()
{
    if (!g_sta_wait_group) {
        g_sta_wait_group = xEventGroupCreate();
        if (!g_sta_wait_group) {
            return ESP_ERR_NO_MEM;
        }
    }
    xEventGroupClearBits(g_sta_wait_group, kBitStaConnected | kBitStaGotIp | kBitStaFailed);
    return ESP_OK;
}

esp_err_t wait_for_sta_bits(EventBits_t desired_bits, int32_t timeout_ms)
{
    if (timeout_ms <= 0) {
        return ESP_OK;
    }

    EventBits_t bits = xEventGroupWaitBits(g_sta_wait_group, desired_bits | kBitStaFailed, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & desired_bits) {
        return ESP_OK;
    }
    if (bits & kBitStaFailed) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

} // namespace

esp_err_t init_once(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "esp_netif_init failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "esp_event_loop_create_default failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = ensure_sta_netif_locked();
    if (err != ESP_OK) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGE(kTag, "esp_wifi_init failed (%s)", esp_err_to_name(err));
        return err;
    }

    if (!g_handlers_registered) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "register WIFI event handler failed (%s)", esp_err_to_name(err));
            return err;
        }

        err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "register IP event handler failed (%s)", esp_err_to_name(err));
            return err;
        }

        g_handlers_registered = true;
    }

    err = clear_sta_wait_bits();
    if (err != ESP_OK) {
        return err;
    }

    g_initialized = true;
    return ESP_OK;
}

esp_err_t subscribe(EventCallback cb, void *user_ctx, Subscription *out_sub)
{
    if (!cb || !out_sub) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < kMaxSubscribers; i++) {
        if (g_subs[i].in_use) {
            continue;
        }
        g_subs[i].in_use = true;
        g_subs[i].id = g_next_sub_id++;
        g_subs[i].cb = cb;
        g_subs[i].user_ctx = user_ctx;
        out_sub->id = g_subs[i].id;
        return ESP_OK;
    }

    return ESP_ERR_NO_MEM;
}

esp_err_t unsubscribe(Subscription sub)
{
    if (sub.id <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < kMaxSubscribers; i++) {
        if (!g_subs[i].in_use || g_subs[i].id != sub.id) {
            continue;
        }
        g_subs[i] = {};
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t get_status(Status *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    Status s{};
    s.mode = Mode::kOff;
    s.sta = StaState::kStopped;
    s.ap = ApState::kStopped;
    s.sta_has_ip = false;
    s.ap_has_ip = false;
    s.sta_ssid[0] = '\0';
    s.sta_rssi = -127;

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        s.mode = mode_from_wifi_mode(mode);
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        s.sta = g_sta_state;
        s.ap = g_ap_state;
    }

    esp_netif_t *sta = g_netif_sta ? g_netif_sta : esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta) {
        esp_netif_ip_info_t ip{};
        if (esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
            s.sta_ip = ip;
            s.sta_has_ip = (ip.ip.addr != 0);
        }
    }

    esp_netif_t *ap = g_netif_ap ? g_netif_ap : esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap) {
        esp_netif_ip_info_t ip{};
        if (esp_netif_get_ip_info(ap, &ip) == ESP_OK) {
            s.ap_ip = ip;
            s.ap_has_ip = (ip.ip.addr != 0);
        }
    }

    wifi_ap_record_t ap_info{};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        const char *ssid = (const char *)ap_info.ssid;
        if (ssid) {
            strncpy(s.sta_ssid, ssid, sizeof(s.sta_ssid) - 1);
            s.sta_ssid[sizeof(s.sta_ssid) - 1] = '\0';
        }
        s.sta_rssi = ap_info.rssi;
        if (s.sta_has_ip) {
            s.sta = StaState::kConnectedHasIp;
        }
        else {
            s.sta = StaState::kConnectedNoIp;
        }
    }
    else {
        if (s.sta == StaState::kConnectedHasIp || s.sta == StaState::kConnectedNoIp) {
            s.sta = StaState::kDisconnected;
        }
    }

    *out = s;
    return ESP_OK;
}

esp_err_t sta_start(void)
{
    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    std::lock_guard<std::mutex> lock(g_mutex);

    err = ensure_sta_netif_locked();
    if (err != ESP_OK) {
        return err;
    }

    wifi_mode_t old_mode = WIFI_MODE_NULL;
    (void)esp_wifi_get_mode(&old_mode);

    g_ap_state = ApState::kStopped;

    err = set_mode_exclusive_locked(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    if (old_mode != WIFI_MODE_STA || g_sta_state == StaState::kStopped) {
        g_sta_state = StaState::kDisconnected;
    }
    return ESP_OK;
}

esp_err_t sta_join_saved(const StaJoinOptions &opts)
{
    esp_err_t err = sta_start();
    if (err != ESP_OK) {
        return err;
    }

    err = clear_sta_wait_bits();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_storage failed (%s)", esp_err_to_name(err));
        return err;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_connect_in_progress = true;
        g_connect_retry_num = 0;
        g_connect_max_retries = opts.max_retries;
        g_sta_state = StaState::kConnecting;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_connect failed (%s)", esp_err_to_name(err));
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_connect_in_progress = false;
        }
        signal_sta_failed();
        return err;
    }

    if (opts.timeout_ms <= 0) {
        return ESP_OK;
    }

    const EventBits_t desired = opts.wait_for_ip ? kBitStaGotIp : kBitStaConnected;
    return wait_for_sta_bits(desired, opts.timeout_ms);
}

esp_err_t sta_join(const StaCredentials &creds, const StaJoinOptions &opts)
{
    if (str_is_empty(creds.ssid)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sta_start();
    if (err != ESP_OK) {
        return err;
    }

    err = clear_sta_wait_bits();
    if (err != ESP_OK) {
        return err;
    }

    const char *password = creds.password ? creds.password : "";

    wifi_storage_t storage = creds.persist_to_flash ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM;
    err = esp_wifi_set_storage(storage);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_storage failed (%s)", esp_err_to_name(err));
        return err;
    }

    wifi_config_t cfg = {};
    strncpy((char *)cfg.sta.ssid, creds.ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.failure_retry_cnt = 1;

    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_config(STA) failed (%s)", esp_err_to_name(err));
        return err;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_connect_in_progress = true;
        g_connect_retry_num = 0;
        g_connect_max_retries = opts.max_retries;
        g_sta_state = StaState::kConnecting;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_connect failed (%s)", esp_err_to_name(err));
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_connect_in_progress = false;
        }
        signal_sta_failed();
        return err;
    }

    if (opts.timeout_ms <= 0) {
        return ESP_OK;
    }

    const EventBits_t desired = opts.wait_for_ip ? kBitStaGotIp : kBitStaConnected;
    return wait_for_sta_bits(desired, opts.timeout_ms);
}

esp_err_t sta_disconnect(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_connect_in_progress = false;
    g_connect_retry_num = 0;
    g_connect_max_retries = 0;

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(kTag, "esp_wifi_disconnect failed (%s)", esp_err_to_name(err));
        return err;
    }

    g_sta_state = StaState::kDisconnected;
    return ESP_OK;
}

esp_err_t ap_start(const SoftApConfig &cfg)
{
    if (str_is_empty(cfg.ssid)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    std::lock_guard<std::mutex> lock(g_mutex);

    err = ensure_ap_netif_locked();
    if (err != ESP_OK) {
        return err;
    }

    g_sta_state = StaState::kStopped;
    g_connect_in_progress = false;

    (void)wifi_stop_locked();

    err = set_mode_exclusive_no_start_locked(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid, cfg.ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = (uint8_t)strnlen(cfg.ssid, sizeof(ap_cfg.ap.ssid));

    const char *password = cfg.password ? cfg.password : "";
    strncpy((char *)ap_cfg.ap.password, password, sizeof(ap_cfg.ap.password) - 1);

    ap_cfg.ap.channel = (cfg.channel != 0) ? cfg.channel : 1;
    ap_cfg.ap.max_connection = (cfg.max_connections != 0) ? cfg.max_connections : 4;
    ap_cfg.ap.ssid_hidden = cfg.hidden ? 1 : 0;
    ap_cfg.ap.authmode = str_is_empty(password) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_set_config(AP) failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = wifi_start_locked();
    if (err != ESP_OK) {
        return err;
    }

    g_ap_state = ApState::kRunning;
    return ESP_OK;
}

esp_err_t ap_stop(void)
{
    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_connect_in_progress = false;
    g_sta_state = StaState::kStopped;
    g_ap_state = ApState::kStopped;

    return wifi_stop_locked();
}

esp_err_t set_hostname(const char *hostname)
{
    if (str_is_empty(hostname)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    strncpy(g_hostname, hostname, sizeof(g_hostname) - 1);
    g_hostname[sizeof(g_hostname) - 1] = '\0';

    if (g_netif_sta) {
        (void)esp_netif_set_hostname(g_netif_sta, g_hostname);
    }
    if (g_netif_ap) {
        (void)esp_netif_set_hostname(g_netif_ap, g_hostname);
    }

    return ESP_OK;
}

esp_err_t start_mdns_http(uint16_t port, const char *hostname, const char *instance_name)
{
    if (port == 0 || str_is_empty(hostname)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_mdns_started || g_mdns_starting) {
            return ESP_OK;
        }
        g_mdns_starting = true;
    }

    (void)set_hostname(hostname);

    err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "mdns_init failed (%s)", esp_err_to_name(err));
        std::lock_guard<std::mutex> lock(g_mutex);
        g_mdns_starting = false;
        return err;
    }

    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "mdns_hostname_set('%s') failed (%s)", hostname, esp_err_to_name(err));
        std::lock_guard<std::mutex> lock(g_mutex);
        g_mdns_starting = false;
        return err;
    }

    const char *instance = (instance_name && instance_name[0] != '\0') ? instance_name : hostname;
    (void)mdns_instance_name_set(instance);

    mdns_txt_item_t service_txt[] = {
        { "path", "/" },
    };

    err = mdns_service_add(nullptr, "_http", "_tcp", port, service_txt, sizeof(service_txt) / sizeof(service_txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "mdns_service_add(_http._tcp:%u) failed (%s)", (unsigned)port, esp_err_to_name(err));
        std::lock_guard<std::mutex> lock(g_mutex);
        g_mdns_starting = false;
        return err;
    }

    ESP_LOGI(kTag, "mDNS started: http://%s.local:%u/", hostname, (unsigned)port);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_mdns_starting = false;
        g_mdns_started = true;
    }
    return ESP_OK;
}

esp_err_t stop_mdns(void)
{
    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_mdns_started) {
        return ESP_OK;
    }

    mdns_free();
    g_mdns_started = false;
    g_mdns_starting = false;
    return ESP_OK;
}

bool mdns_is_running(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_mdns_started;
}

esp_err_t scan_sync(ScanRecord *out_records, size_t *inout_count)
{
    if (!inout_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!out_records && *inout_count != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    err = sta_start();
    if (err != ESP_OK) {
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_start(nullptr, true);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_wifi_scan_start failed (%s)", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_wifi_scan_get_ap_num failed (%s)", esp_err_to_name(err));
        return err;
    }

    if (!out_records) {
        *inout_count = ap_count;
        return ESP_OK;
    }

    constexpr uint16_t kMaxScan = 32;
    wifi_ap_record_t recs[kMaxScan] = {};

    uint16_t number = (uint16_t)*inout_count;
    if (number > kMaxScan) {
        number = kMaxScan;
    }
    if (number > ap_count) {
        number = ap_count;
    }

    err = esp_wifi_scan_get_ap_records(&number, recs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_wifi_scan_get_ap_records failed (%s)", esp_err_to_name(err));
        return err;
    }

    for (uint16_t i = 0; i < number; i++) {
        out_records[i].rssi = recs[i].rssi;
        out_records[i].authmode = recs[i].authmode;
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid) {
            strncpy(out_records[i].ssid, ssid, sizeof(out_records[i].ssid) - 1);
            out_records[i].ssid[sizeof(out_records[i].ssid) - 1] = '\0';
        }
        else {
            out_records[i].ssid[0] = '\0';
        }
    }

    *inout_count = number;
    return ESP_OK;
}

esp_err_t get_sta_mac(uint8_t mac_out[6])
{
    if (!mac_out) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = init_once();
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_get_mac(WIFI_IF_STA, mac_out);
}

esp_err_t ensure_sta_started(void)
{
    return sta_start();
}

esp_err_t sta_connect(void)
{
    esp_err_t err = sta_start();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_wifi_connect failed (%s)", esp_err_to_name(err));
    }
    return err;
}

bool sta_has_ip(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_sta_state == StaState::kConnectedHasIp;
}

esp_err_t get_sta_ip_info(esp_netif_ip_info_t *out_ip)
{
    if (!out_ip) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_netif_sta) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_netif_get_ip_info(g_netif_sta, out_ip);
}

esp_netif_t *netif_sta(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_netif_sta;
}

esp_err_t start_softap(const char *ssid, const char *password)
{
    if (str_is_empty(ssid) || str_is_empty(password)) {
        return ESP_ERR_INVALID_ARG;
    }

    SoftApConfig cfg{};
    cfg.ssid = ssid;
    cfg.password = password;
    return ap_start(cfg);
}

esp_err_t stop_softap(void)
{
    return ap_stop();
}

bool softap_is_running(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_ap_state == ApState::kRunning;
}

esp_err_t get_softap_ip_info(esp_netif_ip_info_t *out_ip)
{
    if (!out_ip) {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_netif_ap) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_netif_get_ip_info(g_netif_ap, out_ip);
}

esp_netif_t *netif_softap(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_netif_ap;
}

} // namespace wifi
