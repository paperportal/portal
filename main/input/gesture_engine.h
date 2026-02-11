#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

class GestureEngine {
public:
    enum class TouchType {
        Down,
        Move,
        Up,
        Cancel,
    };

    struct TouchEvent {
        TouchType type;
        int pointer_id;
        float x;
        float y;
        uint64_t time_ms;
    };

    struct PointF {
        float x;
        float y;
    };

    struct GestureDef {
        int32_t handle;
        char id[48];
        std::vector<PointF> points;
        float tolerance_px;
        bool fixed;
        bool system;
        int32_t priority;
        uint32_t max_duration_ms;
        bool segment_constraint_enabled;
    };

    struct TrackState {
        bool active;
        PointF anchor;
        uint64_t start_time_ms;
        size_t target_index;
        float last_dist_to_target;
        bool approach_armed;
        float max_progress;
        int consecutive_fail_approach;
        int consecutive_fail_segment;
        PointF down_pos;
        PointF last_pos;
    };

    GestureEngine();

    void ClearAll();
    void ClearCustom();
    void ResetTracking();

    int32_t RegisterPolyline(const char *id_z, std::vector<PointF> points, bool fixed, float tolerance_px,
        int32_t priority, uint32_t max_duration_ms, bool segment_constraint_enabled, bool system = false);
    int32_t Remove(int32_t handle);

    // Feed the engine a touch event. Returns the winning handle on Up, or 0 if no match.
    int32_t ProcessTouchEvent(const TouchEvent &event);

private:
    struct Slot {
        GestureDef def{};
        TrackState track{};
    };

    static constexpr int kConsecutiveFailThreshold = 2;

    int32_t next_handle_ = 1;
    int active_pointer_id_ = -1;
    bool touch_active_ = false;
    std::vector<Slot> slots_;

    static float dist_sq(const PointF &a, const PointF &b);
    static float dist_sq_point_to_segment(const PointF &p, const PointF &a, const PointF &b);

    static PointF abs_point(const GestureDef &def, const TrackState &track, size_t index);
    static void reset_track(TrackState &track);

    void on_down(const TouchEvent &event);
    void on_move_or_up(const TouchEvent &event);
    int32_t on_up_and_select_winner(const TouchEvent &event);
};

GestureEngine &gesture_engine();
