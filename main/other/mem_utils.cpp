#include "mem_utils.h"

#include <inttypes.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_HEAP_TRACING_STANDALONE || CONFIG_HEAP_TRACING_TOHOST
#include "esp_heap_trace.h"
#endif

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace mem_utils {

static constexpr const char *kTag = "mem_utils";

static void log_heap_caps(const char *tag, const char *label, const char *name, uint32_t caps)
{
    const size_t total = heap_caps_get_total_size(caps);
    if (total == 0) {
        return;
    }

    multi_heap_info_t info{};
    heap_caps_get_info(&info, caps);

    float largest_pct = 0.0f;
    if (info.total_free_bytes > 0) {
        largest_pct = (static_cast<float>(info.largest_free_block) * 100.0f) /
                      static_cast<float>(info.total_free_bytes);
    }

    ESP_LOGI(tag,
             "[%s] heap %-8s caps=0x%08" PRIx32 " total=%" PRIu32 " free=%" PRIu32 " (min=%" PRIu32
             ", largest=%" PRIu32 ", largest/free=%.1f%%) alloc=%" PRIu32 " blocks=%" PRIu32 "/%" PRIu32,
             label ? label : "", name, caps, static_cast<uint32_t>(total), static_cast<uint32_t>(info.total_free_bytes),
             static_cast<uint32_t>(info.minimum_free_bytes), static_cast<uint32_t>(info.largest_free_block),
             largest_pct, static_cast<uint32_t>(info.total_allocated_bytes), static_cast<uint32_t>(info.allocated_blocks),
             static_cast<uint32_t>(info.total_blocks));
}

static void alloc_failed_hook(size_t size, uint32_t caps, const char *function_name)
{
    ESP_EARLY_LOGE(kTag, "alloc failed: size=%" PRIu32 " caps=0x%08" PRIx32 " func=%s", static_cast<uint32_t>(size), caps,
                   function_name ? function_name : "?");

    const size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#ifdef MALLOC_CAP_SPIRAM
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const size_t free_psram = 0;
#endif

    ESP_EARLY_LOGE(kTag, "heap free now: 8bit=%" PRIu32 " internal=%" PRIu32 " psram=%" PRIu32,
                   static_cast<uint32_t>(free_8bit), static_cast<uint32_t>(free_int), static_cast<uint32_t>(free_psram));
}

void init()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    (void)heap_caps_register_failed_alloc_callback(&alloc_failed_hook);

    ESP_LOGI(kTag, "Heap poisoning: %s",
#if CONFIG_HEAP_POISONING_LIGHT
             "light"
#elif CONFIG_HEAP_POISONING_COMPREHENSIVE
             "comprehensive"
#else
             "disabled"
#endif
    );

    ESP_LOGI(kTag, "Heap tracing: %s",
#if CONFIG_HEAP_TRACING_STANDALONE
             "standalone"
#elif CONFIG_HEAP_TRACING_TOHOST
             "tohost"
#else
             "disabled"
#endif
    );

#if CONFIG_HEAP_TRACING_STANDALONE || CONFIG_HEAP_TRACING_TOHOST
    ESP_LOGI(kTag, "Heap tracing stack depth: %d", CONFIG_HEAP_TRACING_STACK_DEPTH);
#endif

    ESP_LOGI(kTag, "FreeRTOS trace facility: %s",
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
             "enabled"
#else
             "disabled"
#endif
    );

    log_heap(kTag, "init");
    log_stack(kTag, "init");
}

void log_heap_brief(const char *tag, const char *label)
{
    if (!tag) {
        tag = kTag;
    }
    if (!label) {
        label = "";
    }

    const size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

#ifdef MALLOC_CAP_SPIRAM
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const size_t psram_total = 0;
    const size_t psram_free = 0;
    const size_t psram_min_free = 0;
#endif

    ESP_LOGI(tag,
             "[%s] heap total=%" PRIu32 " free=%" PRIu32 " min_free=%" PRIu32
             " | psram total=%" PRIu32 " free=%" PRIu32 " min_free=%" PRIu32,
             label,
             static_cast<uint32_t>(internal_total),
             static_cast<uint32_t>(internal_free),
             static_cast<uint32_t>(internal_min_free),
             static_cast<uint32_t>(psram_total),
             static_cast<uint32_t>(psram_free),
             static_cast<uint32_t>(psram_min_free));
}

void log_heap(const char *tag, const char *label)
{
    if (!tag) {
        tag = kTag;
    }
    if (!label) {
        label = "";
    }

    // Commonly useful breakdowns. Only prints entries that exist (total > 0).
    log_heap_caps(tag, label, "8bit", MALLOC_CAP_8BIT);
    log_heap_caps(tag, label, "internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#ifdef MALLOC_CAP_SPIRAM
    log_heap_caps(tag, label, "psram", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    log_heap_caps(tag, label, "dma", MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    log_heap_caps(tag, label, "32bit", MALLOC_CAP_32BIT);
    log_heap_caps(tag, label, "exec", MALLOC_CAP_EXEC);
#ifdef MALLOC_CAP_RTCRAM
    log_heap_caps(tag, label, "rtcram", MALLOC_CAP_RTCRAM);
#endif
#ifdef MALLOC_CAP_TCM
    log_heap_caps(tag, label, "tcm", MALLOC_CAP_TCM);
#endif
}

void log_stack(const char *tag, const char *label)
{
    if (!tag) {
        tag = kTag;
    }
    if (!label) {
        label = "";
    }

    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    const char *task_name = pcTaskGetName(task);

#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    const UBaseType_t watermark_words = uxTaskGetStackHighWaterMark(task);
    const uint32_t watermark_bytes = static_cast<uint32_t>(watermark_words * sizeof(StackType_t));
    ESP_LOGI(tag, "[%s] stack current_task='%s' high_water_mark=%" PRIu32 " bytes", label, task_name ? task_name : "?",
             watermark_bytes);
#else
    ESP_LOGI(tag, "[%s] stack current_task='%s' high_water_mark=unavailable (enable INCLUDE_uxTaskGetStackHighWaterMark)",
             label, task_name ? task_name : "?");
#endif

#if (configUSE_TRACE_FACILITY == 1) && defined(INCLUDE_uxTaskGetSystemState) && (INCLUDE_uxTaskGetSystemState == 1)
    const UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    if (num_tasks == 0) {
        return;
    }

    auto *statuses = static_cast<TaskStatus_t *>(
        heap_caps_malloc(num_tasks * sizeof(TaskStatus_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!statuses) {
        ESP_LOGW(tag, "[%s] stack all_tasks: failed to allocate status buffer (%" PRIu32 " bytes)", label,
                 static_cast<uint32_t>(num_tasks * sizeof(TaskStatus_t)));
        return;
    }

    uint32_t total_runtime = 0;
    const UBaseType_t got = uxTaskGetSystemState(statuses, num_tasks, &total_runtime);
    for (UBaseType_t i = 0; i < got; i++) {
        const TaskStatus_t &s = statuses[i];
        const uint32_t hw_bytes = static_cast<uint32_t>(s.usStackHighWaterMark * sizeof(StackType_t));
        ESP_LOGI(tag, "[%s] stack task='%s' prio=%" PRIu32 " state=%d high_water_mark=%" PRIu32 " bytes", label, s.pcTaskName,
                 static_cast<uint32_t>(s.uxCurrentPriority), static_cast<int>(s.eCurrentState), hw_bytes);
    }

    heap_caps_free(statuses);
#endif
}

void log_state(const char *tag, const char *label)
{
    log_heap(tag, label);
    log_stack(tag, label);
}

bool check_heap_integrity(const char *tag, const char *label, bool print_errors)
{
    if (!tag) { tag = kTag; }
    if (!label) { label = ""; }
    ESP_LOGI(tag, "[%s] checking heap integrity…", label);
    const bool ok = heap_caps_check_integrity_all(print_errors);
    if (ok) {
        ESP_LOGI(tag, "[%s] heap integrity: OK", label);
    } else {
        ESP_LOGE(tag, "[%s] heap integrity: FAILED", label);
    }
    return ok;
}

bool check_heap_integrity_split(const char *tag, const char *label, bool print_errors)
{
    if (!tag) {
        tag = kTag;
    }
    if (!label) {
        label = "";
    }

    bool ok = true;

    {
        ESP_LOGI(tag, "[%s] checking heap integrity (internal)…", label);
        const bool internal_ok = heap_caps_check_integrity(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, print_errors);
        if (internal_ok) {
            ESP_LOGI(tag, "[%s] heap integrity (internal): OK", label);
        }
        else {
            ESP_LOGE(tag, "[%s] heap integrity (internal): FAILED", label);
            ok = false;
        }
    }

#ifdef MALLOC_CAP_SPIRAM
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (psram_total > 0) {
        ESP_LOGI(tag, "[%s] checking heap integrity (psram)…", label);
        const bool psram_ok = heap_caps_check_integrity(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, print_errors);
        if (psram_ok) {
            ESP_LOGI(tag, "[%s] heap integrity (psram): OK", label);
        }
        else {
            ESP_LOGE(tag, "[%s] heap integrity (psram): FAILED", label);
            ok = false;
        }
    }
#endif

    return ok;
}

bool heap_trace_start(const char *tag, const char *label)
{
    if (!tag) {
        tag = kTag;
    }
    if (!label) {
        label = "";
    }

#if CONFIG_HEAP_TRACING_STANDALONE || CONFIG_HEAP_TRACING_TOHOST
    static bool initialized = false;

    if (!initialized) {
#if CONFIG_HEAP_TRACING_STANDALONE
        static heap_trace_record_t s_records[256];
        esp_err_t err = ::heap_trace_init_standalone(s_records, sizeof(s_records) / sizeof(s_records[0]));
#elif CONFIG_HEAP_TRACING_TOHOST
        esp_err_t err = ::heap_trace_init_tohost();
#else
        esp_err_t err = ESP_ERR_NOT_SUPPORTED;
#endif
        if (err != ESP_OK) {
            ESP_LOGW(tag, "[%s] heap trace init failed: %s", label, esp_err_to_name(err));
            return false;
        }
        initialized = true;
    }

    esp_err_t err = ::heap_trace_start(HEAP_TRACE_LEAKS);
    if (err != ESP_OK) {
        ESP_LOGW(tag, "[%s] heap trace start failed: %s", label, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(tag, "[%s] heap trace started (mode=leaks)", label);
    return true;
#else
    ESP_LOGW(tag,
             "[%s] heap trace unavailable (enable: Component config -> Heap memory debugging -> Heap tracing)",
             label);
    return false;
#endif
}

bool heap_trace_stop_and_dump(const char *tag, const char *label)
{
    if (!tag) {
        tag = kTag;
    }
    if (!label) {
        label = "";
    }

#if CONFIG_HEAP_TRACING_STANDALONE || CONFIG_HEAP_TRACING_TOHOST
    esp_err_t err = ::heap_trace_stop();
    if (err != ESP_OK) {
        ESP_LOGW(tag, "[%s] heap trace stop failed: %s", label, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(tag, "[%s] heap trace dump:", label);
    ::heap_trace_dump();
    return true;
#else
    ESP_LOGW(tag,
             "[%s] heap trace unavailable (enable: Component config -> Heap memory debugging -> Heap tracing)",
             label);
    return false;
#endif
}

} // namespace mem_utils
