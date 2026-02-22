#pragma once

#include <stdint.h>

class WasmController;

class MicroTaskScheduler {
public:
    static constexpr uint32_t kNoDueMs = 0xffffffffu;
    static constexpr uint32_t kDefaultYieldDelayMs = 50;
    static constexpr int32_t kMaxTasks = 64;

    int32_t Start(uint32_t start_after_ms, uint32_t period_ms);
    int32_t Cancel(int32_t handle);
    void ClearAll();

    bool HasTasks() const;
    bool HasDue(uint32_t now_ms) const;
    uint32_t NextDueMs() const;
    uint32_t NextDueMs(uint32_t now_ms) const;

    void RunDue(WasmController *wasm, uint32_t now_ms, int max_steps);

private:
    struct Slot {
        bool occupied = false;
        uint16_t generation = 1;
        uint32_t next_run_ms = 0;
        uint32_t period_ms = 0;
    };

    static constexpr uint16_t kMaxGeneration = 0x7fff;

    static bool TimeReached(uint32_t now_ms, uint32_t target_ms);
    static uint16_t NextGeneration(uint16_t generation);
    static int32_t EncodeHandle(uint16_t index, uint16_t generation);
    static bool DecodeHandle(int32_t handle, uint16_t *out_index, uint16_t *out_generation);
    static uint32_t DelayForSleep(uint32_t requested_ms);
    static uint32_t NextPeriodicBoundary(uint32_t previous_due_ms, uint32_t period_ms, uint32_t now_ms);

    Slot *ResolveHandle(int32_t handle);
    const Slot *ResolveHandle(int32_t handle) const;
    void ReleaseSlot(Slot *slot);
    int FindDueSlot(uint32_t now_ms) const;

    Slot slots_[kMaxTasks] = {};
    uint16_t run_cursor_ = 0;
    uint16_t alloc_cursor_ = 0;
    int32_t task_count_ = 0;
};

MicroTaskScheduler &microtask_scheduler();
