#include "host/microtask_scheduler.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "wasm/app_contract.h"
#include "wasm/wasm_controller.h"

namespace {

constexpr const char *kTag = "microtask_scheduler";

uint32_t action_kind(int64_t action)
{
    const uint64_t bits = (uint64_t)action;
    return (uint32_t)(bits >> 32);
}

uint32_t action_arg(int64_t action)
{
    const uint64_t bits = (uint64_t)action;
    return (uint32_t)(bits & 0xffffffffu);
}

} // namespace

int32_t MicroTaskScheduler::Start(uint32_t start_after_ms, uint32_t period_ms)
{
    if (task_count_ >= kMaxTasks) {
        return 0;
    }

    const uint32_t now_ms = (uint32_t)((int32_t)(esp_timer_get_time() / 1000));
    for (uint16_t i = 0; i < kMaxTasks; i++) {
        const uint16_t index = (uint16_t)((alloc_cursor_ + i) % kMaxTasks);
        Slot &slot = slots_[index];
        if (slot.occupied) {
            continue;
        }

        slot.occupied = true;
        slot.next_run_ms = now_ms + start_after_ms;
        slot.period_ms = period_ms;
        if (slot.generation == 0 || slot.generation > kMaxGeneration) {
            slot.generation = 1;
        }

        task_count_++;
        alloc_cursor_ = (uint16_t)((index + 1) % kMaxTasks);
        return EncodeHandle(index, slot.generation);
    }

    return 0;
}

int32_t MicroTaskScheduler::Cancel(int32_t handle)
{
    Slot *slot = ResolveHandle(handle);
    if (!slot) {
        return -1;
    }

    ReleaseSlot(slot);
    return 0;
}

void MicroTaskScheduler::ClearAll()
{
    for (Slot &slot : slots_) {
        if (!slot.occupied) {
            continue;
        }
        ReleaseSlot(&slot);
    }
    run_cursor_ = 0;
    alloc_cursor_ = 0;
}

bool MicroTaskScheduler::HasTasks() const
{
    return task_count_ > 0;
}

bool MicroTaskScheduler::HasDue(uint32_t now_ms) const
{
    return FindDueSlot(now_ms) >= 0;
}

uint32_t MicroTaskScheduler::NextDueMs() const
{
    const uint32_t now_ms = (uint32_t)((int32_t)(esp_timer_get_time() / 1000));
    return NextDueMs(now_ms);
}

uint32_t MicroTaskScheduler::NextDueMs(uint32_t now_ms) const
{
    uint32_t best = kNoDueMs;
    uint32_t best_wait = 0;
    for (const Slot &slot : slots_) {
        if (!slot.occupied) {
            continue;
        }

        const uint32_t wait_ms = TimeReached(now_ms, slot.next_run_ms) ? 0 : (uint32_t)(slot.next_run_ms - now_ms);
        if (best == kNoDueMs || wait_ms < best_wait) {
            best = slot.next_run_ms;
            best_wait = wait_ms;
        }
    }
    return best;
}

void MicroTaskScheduler::RunDue(WasmController *wasm, uint32_t now_ms, int max_steps)
{
    if (!wasm || max_steps <= 0 || task_count_ <= 0) {
        return;
    }
    if (!wasm->HasMicroTaskStepHandler()) {
        return;
    }

    int remaining_steps = max_steps;
    while (remaining_steps > 0 && task_count_ > 0) {
        const int due_index = FindDueSlot(now_ms);
        if (due_index < 0) {
            break;
        }

        Slot &due_slot = slots_[due_index];
        const int32_t handle = EncodeHandle((uint16_t)due_index, due_slot.generation);
        const uint32_t previous_due_ms = due_slot.next_run_ms;
        const uint32_t period_ms = due_slot.period_ms;
        run_cursor_ = (uint16_t)((due_index + 1) % kMaxTasks);

        int64_t action = 0;
        if (!wasm->CallMicroTaskStep(handle, (int32_t)now_ms, &action)) {
            return;
        }

        Slot *slot = ResolveHandle(handle);
        if (!slot) {
            remaining_steps--;
            continue;
        }

        const uint32_t kind = action_kind(action);
        const uint32_t arg = action_arg(action);

        if (kind == pp_contract::kMicroTaskActionDone) {
            ReleaseSlot(slot);
            remaining_steps--;
            continue;
        }

        if (kind == pp_contract::kMicroTaskActionYield) {
            if (period_ms != 0) {
                slot->next_run_ms = NextPeriodicBoundary(previous_due_ms, period_ms, now_ms);
            }
            else {
                slot->next_run_ms = now_ms + kDefaultYieldDelayMs;
            }
            remaining_steps--;
            continue;
        }

        if (kind == pp_contract::kMicroTaskActionSleepMs) {
            const uint32_t sleep_due_ms = now_ms + DelayForSleep(arg);
            if (period_ms != 0) {
                const uint32_t period_due_ms = NextPeriodicBoundary(previous_due_ms, period_ms, now_ms);
                slot->next_run_ms = TimeReached(sleep_due_ms, period_due_ms) ? sleep_due_ms : period_due_ms;
            }
            else {
                slot->next_run_ms = sleep_due_ms;
            }
            remaining_steps--;
            continue;
        }

        ESP_LOGW(kTag, "Task handle=%" PRId32 " returned invalid action kind=%" PRIu32 "; removing", handle, kind);
        ReleaseSlot(slot);
        remaining_steps--;
    }
}

bool MicroTaskScheduler::TimeReached(uint32_t now_ms, uint32_t target_ms)
{
    return (uint32_t)(now_ms - target_ms) < 0x80000000u;
}

uint16_t MicroTaskScheduler::NextGeneration(uint16_t generation)
{
    uint16_t next = (uint16_t)(generation + 1);
    if (next == 0 || next > kMaxGeneration) {
        next = 1;
    }
    return next;
}

int32_t MicroTaskScheduler::EncodeHandle(uint16_t index, uint16_t generation)
{
    if (index >= kMaxTasks || generation == 0 || generation > kMaxGeneration) {
        return 0;
    }
    const uint32_t raw = ((uint32_t)generation << 16) | ((uint32_t)index + 1u);
    return (int32_t)raw;
}

bool MicroTaskScheduler::DecodeHandle(int32_t handle, uint16_t *out_index, uint16_t *out_generation)
{
    if (handle <= 0 || !out_index || !out_generation) {
        return false;
    }

    const uint32_t raw = (uint32_t)handle;
    const uint16_t index_part = (uint16_t)(raw & 0xffffu);
    const uint16_t generation = (uint16_t)((raw >> 16) & 0x7fffu);
    if (index_part == 0 || index_part > kMaxTasks || generation == 0) {
        return false;
    }

    *out_index = (uint16_t)(index_part - 1);
    *out_generation = generation;
    return true;
}

uint32_t MicroTaskScheduler::DelayForSleep(uint32_t requested_ms)
{
    return requested_ms == 0 ? kDefaultYieldDelayMs : requested_ms;
}

uint32_t MicroTaskScheduler::NextPeriodicBoundary(uint32_t previous_due_ms, uint32_t period_ms, uint32_t now_ms)
{
    if (period_ms == 0) {
        return previous_due_ms;
    }
    if (!TimeReached(now_ms, previous_due_ms)) {
        return previous_due_ms;
    }

    const uint32_t elapsed = now_ms - previous_due_ms;
    const uint32_t periods_to_advance = (elapsed / period_ms) + 1u;
    return previous_due_ms + periods_to_advance * period_ms;
}

MicroTaskScheduler::Slot *MicroTaskScheduler::ResolveHandle(int32_t handle)
{
    uint16_t index = 0;
    uint16_t generation = 0;
    if (!DecodeHandle(handle, &index, &generation)) {
        return nullptr;
    }

    Slot &slot = slots_[index];
    if (!slot.occupied || slot.generation != generation) {
        return nullptr;
    }
    return &slot;
}

const MicroTaskScheduler::Slot *MicroTaskScheduler::ResolveHandle(int32_t handle) const
{
    uint16_t index = 0;
    uint16_t generation = 0;
    if (!DecodeHandle(handle, &index, &generation)) {
        return nullptr;
    }

    const Slot &slot = slots_[index];
    if (!slot.occupied || slot.generation != generation) {
        return nullptr;
    }
    return &slot;
}

void MicroTaskScheduler::ReleaseSlot(Slot *slot)
{
    if (!slot || !slot->occupied) {
        return;
    }

    slot->occupied = false;
    slot->next_run_ms = 0;
    slot->period_ms = 0;
    slot->generation = NextGeneration(slot->generation);
    if (task_count_ > 0) {
        task_count_--;
    }
}

int MicroTaskScheduler::FindDueSlot(uint32_t now_ms) const
{
    if (task_count_ <= 0) {
        return -1;
    }

    for (uint16_t i = 0; i < kMaxTasks; i++) {
        const uint16_t index = (uint16_t)((run_cursor_ + i) % kMaxTasks);
        const Slot &slot = slots_[index];
        if (!slot.occupied) {
            continue;
        }
        if (TimeReached(now_ms, slot.next_run_ms)) {
            return (int)index;
        }
    }
    return -1;
}

MicroTaskScheduler &microtask_scheduler()
{
    static MicroTaskScheduler scheduler;
    return scheduler;
}
