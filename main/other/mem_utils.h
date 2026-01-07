#pragma once

#include <cstdint>

/**
 * Memory debugging toolbox (ESP-IDF checklist)
 *
 * This project runs on ESP-IDF, which has a lot of built-in memory debugging features. This header is both an API
 * surface (see functions below) and a quick reference for what you can enable/use when chasing OOMs, leaks, heap
 * corruption, stack overflows, or fragmentation.
 *
 * Menuconfig features (compile-time)
 * - Heap corruption detection (Component config -> Heap memory debugging):
 *   - `CONFIG_HEAP_POISONING_LIGHT` / `CONFIG_HEAP_POISONING_COMPREHENSIVE` (detect over/underwrites & use-after-free)
 * - Heap tracing / leak detection (Component config -> Heap memory debugging):
 *   - `CONFIG_HEAP_TRACING_STANDALONE` or `CONFIG_HEAP_TRACING_TOHOST`
 *   - `CONFIG_HEAP_TRACING_STACK_DEPTH` (stores backtrace frames per alloc/free)
 *   - `CONFIG_HEAP_TRACE_HASH_MAP`, `CONFIG_HEAP_TRACE_HASH_MAP_IN_EXT_RAM`, `CONFIG_HEAP_TRACE_HASH_MAP_SIZE`
 * - Allocation/free hooks (Component config -> Heap memory debugging):
 *   - `CONFIG_HEAP_USE_HOOKS` + implement `esp_heap_trace_alloc_hook()` / `esp_heap_trace_free_hook()`
 * - Task attribution for allocations (Component config -> Heap memory debugging):
 *   - `CONFIG_HEAP_TASK_TRACKING` (adds per-allocation overhead; requires heap poisoning)
 *   - API: `esp_heap_task_info.h` / `heap_caps_get_per_task_info()` (per-task totals + optional block list)
 * - Fail-fast on OOM (Component config -> Heap memory debugging):
 *   - `CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS`
 * - Heap implementation / placement (Component config -> Heap memory debugging):
 *   - `CONFIG_HEAP_TLSF_USE_ROM_IMPL` (use ROM heap; harder to debug allocator internals)
 *   - `CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH` (saves IRAM; avoid calling heap APIs from ISR if enabled)
 *
 * FreeRTOS stack debugging (compile-time)
 * - Stack overflow checks (Component config -> FreeRTOS -> Kernel):
 *   - `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_*` (none / method 1 / method 2 (canary))
 *   - Provide/inspect `vApplicationStackOverflowHook()` output on overflow
 * - Early stack overflow watchpoint (Component config -> FreeRTOS -> Port):
 *   - `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK` (uses a HW watchpoint near end-of-stack)
 * - Stack high-water marks / task snapshots (requires FreeRTOS options):
 *   - `INCLUDE_uxTaskGetStackHighWaterMark` for `uxTaskGetStackHighWaterMark()`
 *   - `configUSE_TRACE_FACILITY` + `INCLUDE_uxTaskGetSystemState` for `uxTaskGetSystemState()`
 *
 * Compiler/runtime stack checking (compile-time)
 * - `CONFIG_COMPILER_STACK_CHECK` + `CONFIG_COMPILER_STACK_CHECK_MODE_*` (adds stack checks in generated code)
 *
 * Crash-time analysis tools (compile-time + host tooling)
 * - Core dumps:
 *   - `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` / `CONFIG_ESP_COREDUMP_ENABLE_TO_UART`
 *   - Analyze with `espcoredump.py` / `idf.py coredump-*`
 * - Panic & debugger integration:
 *   - `CONFIG_ESP_SYSTEM_PANIC_GDBSTUB`, `CONFIG_ESP_GDBSTUB_ENABLED` (enter GDB stub on crash, get backtraces, inspect memory)
 *   - JTAG/OpenOCD: hardware watchpoints, memory inspection, break on write to a corrupting address
 * - Memory protection (target dependent):
 *   - `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE` (catch some illegal IRAM/DRAM accesses via hardware memory protection)
 *
 * Runtime APIs you can call (no rebuild required unless noted)
 * - Heap stats/fragmentation:
 *   - `esp_get_free_heap_size()`, `esp_get_free_internal_heap_size()`, `esp_get_minimum_free_heap_size()`
 *   - `heap_caps_get_info()`, `heap_caps_get_free_size()`, `heap_caps_get_largest_free_block()`,
 *     `heap_caps_get_minimum_free_size()`
 *   - `heap_caps_monitor_local_minimum_free_size_start()` / `_stop()` (measure a "local" low-watermark)
 * - Heap integrity / corruption checks:
 *   - `heap_caps_check_integrity_all()`, `heap_caps_check_integrity()`, `heap_caps_check_integrity_addr()`
 *   - `heap_caps_dump()` / `heap_caps_dump_all()` / `heap_caps_print_heap_info()` / `heap_caps_walk_all()`
 *   - `heap_caps_get_allocated_size(ptr)` (asserts if ptr isn't a valid allocated block)
 * - Per-task heap usage (requires `CONFIG_HEAP_TASK_TRACKING`):
 *   - `heap_caps_get_per_task_info()` (totals and/or per-block ownership information)
 * - OOM visibility:
 *   - `heap_caps_register_failed_alloc_callback()` (this module registers a callback in `mem_utils::init()`)
 *
 * Notes / caveats
 * - `heap_caps_check_integrity*()` can be slow with PSRAM heaps; if you call it frequently you may need to increase
 *   `CONFIG_ESP_INT_WDT_TIMEOUT_MS` (ESP-IDF note).
 * - Heap hooks/tracing callbacks run in allocator context; keep them IRAM-safe and avoid heavy logging/locking.
 * - External RAM (PSRAM/SPIRAM) is tracked via `MALLOC_CAP_SPIRAM` and `CONFIG_SPIRAM`.
 *
 * # ESP-IDF has a few built-in size analyzers (plus standard ELF tools):
 *  - idf.py size (overall IRAM/DRAM/flash usage summary)
 *  - idf.py size-components (breakdown by component)
 *  - idf.py size-files (breakdown by object file; great for “what grew?” diffs)
 *  - Linker map file: build/<project>.map (most detailed; shows section placement and symbols)
 *  - ELF tools for symbol-level blame:
 *      - xtensa-esp32-elf-nm -S --size-sort build/<project>.elf | tail
 *      - xtensa-esp32-elf-size -A build/<project>.elf
 */
namespace mem_utils {

// Registers memory-related hooks and logs build-time memory debug configuration.
void init();

// Logs a snapshot of memory state (heap + stack).
void log_state(const char *tag, const char *label);

// Logs a brief heap snapshot (internal + PSRAM if present).
void log_heap_brief(const char *tag, const char *label);

// Logs heap state split by capability classes (internal RAM, PSRAM, DMA, etc).
void log_heap(const char *tag, const char *label);

// Logs stack high-water marks (current task always; all tasks if enabled in FreeRTOS config).
void log_stack(const char *tag, const char *label);

// Checks heap integrity to help detect heap corruption.
// Returns true if all heaps are valid.
bool check_heap_integrity(const char *tag, const char *label, bool print_errors = true);

// Checks heap integrity split by the most common heap regions (internal RAM + PSRAM if present).
// This is often more actionable than `check_heap_integrity()` when you suspect SPIRAM corruption.
// Returns true if all checked heaps are valid.
bool check_heap_integrity_split(const char *tag, const char *label, bool print_errors = true);

// Heap leak tracing helpers (require enabling heap tracing in menuconfig).
bool heap_trace_start(const char *tag, const char *label);
bool heap_trace_stop_and_dump(const char *tag, const char *label);

} // namespace mem_utils
