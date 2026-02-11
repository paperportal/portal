#include "input/gesture_engine.h"

#include <algorithm>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "esp_log.h"

namespace {
constexpr const char *kTag = "gesture_engine";
} // namespace

GestureEngine::GestureEngine()
{
    ResetTracking();
}

void GestureEngine::reset_track(TrackState &track)
{
    track.active = false;
    track.anchor = { 0.0f, 0.0f };
    track.start_time_ms = 0;
    track.target_index = 0;
    track.last_dist_to_target = 0.0f;
    track.approach_armed = false;
    track.max_progress = 0.0f;
    track.consecutive_fail_approach = 0;
    track.consecutive_fail_segment = 0;
    track.down_pos = { 0.0f, 0.0f };
    track.last_pos = { 0.0f, 0.0f };
}

void GestureEngine::ResetTracking()
{
    touch_active_ = false;
    active_pointer_id_ = -1;
    for (auto &s : slots_) {
        reset_track(s.track);
    }
}

void GestureEngine::ClearAll()
{
    if (!slots_.empty()) {
        ESP_LOGI(kTag, "ClearAll: clearing %u registered gestures (x=%.1f y=%.1f)", (unsigned)slots_.size(), 0.0f, 0.0f);
    }
    slots_.clear();
    ResetTracking();
}

void GestureEngine::ClearCustom()
{
    if (slots_.empty()) {
        return;
    }

    size_t before = slots_.size();
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [](const Slot &s) { return !s.def.system; }), slots_.end());
    const size_t after = slots_.size();
    if (before != after) {
        ESP_LOGI(kTag, "ClearCustom: cleared %u gestures; kept %u system gestures", (unsigned)(before - after), (unsigned)after);
    }
    ResetTracking();
}

float GestureEngine::dist_sq(const PointF &a, const PointF &b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

float GestureEngine::dist_sq_point_to_segment(const PointF &p, const PointF &a, const PointF &b)
{
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float apx = p.x - a.x;
    const float apy = p.y - a.y;

    const float ab_len_sq = abx * abx + aby * aby;
    if (ab_len_sq <= 0.000001f) {
        return dist_sq(p, a);
    }

    float t = (apx * abx + apy * aby) / ab_len_sq;
    if (t < 0.0f) {
        t = 0.0f;
    }
    else if (t > 1.0f) {
        t = 1.0f;
    }

    const PointF proj = { a.x + t * abx, a.y + t * aby };
    return dist_sq(p, proj);
}

GestureEngine::PointF GestureEngine::abs_point(const GestureDef &def, const TrackState &track, size_t index)
{
    if (index >= def.points.size()) {
        return { 0.0f, 0.0f };
    }
    const PointF p = def.points[index];
    if (def.fixed) {
        return p;
    }
    return { track.anchor.x + p.x, track.anchor.y + p.y };
}

int32_t GestureEngine::RegisterPolyline(const char *id_z, std::vector<PointF> points, bool fixed, float tolerance_px,
    int32_t priority, uint32_t max_duration_ms, bool segment_constraint_enabled, bool system)
{
    if (!id_z) {
        return -1;
    }
    const size_t id_len = strnlen(id_z, sizeof(GestureDef::id));
    if (id_len == 0 || id_len >= sizeof(GestureDef::id)) {
        return -1;
    }
    if (points.size() < 2) {
        return -1;
    }
    if (!(tolerance_px > 0.0f)) {
        return -1;
    }

    Slot s{};
    s.def.handle = next_handle_++;
    memset(s.def.id, 0, sizeof(s.def.id));
    memcpy(s.def.id, id_z, id_len);
    s.def.points = std::move(points);
    s.def.tolerance_px = tolerance_px;
    s.def.fixed = fixed;
    s.def.system = system;
    s.def.priority = priority;
    s.def.max_duration_ms = max_duration_ms;
    s.def.segment_constraint_enabled = segment_constraint_enabled;

    reset_track(s.track);
    slots_.push_back(std::move(s));

    const auto &def = slots_.back().def;
    const PointF p0 = def.points.empty() ? PointF{ 0.0f, 0.0f } : def.points.front();
    const PointF plast = def.points.empty() ? PointF{ 0.0f, 0.0f } : def.points.back();
    ESP_LOGI(kTag,
        "RegisterPolyline: id='%s' handle=%" PRIi32 " points=%u fixed=%d system=%d tol=%.1f pri=%" PRIi32 " max_dur=%" PRIu32
        " seg=%d p0=(%.1f,%.1f) plast=(%.1f,%.1f)",
        def.id, def.handle, (unsigned)def.points.size(), fixed ? 1 : 0, system ? 1 : 0, tolerance_px, priority,
        max_duration_ms, segment_constraint_enabled ? 1 : 0, p0.x, p0.y, plast.x, plast.y);

    return slots_.back().def.handle;
}

int32_t GestureEngine::Remove(int32_t handle)
{
    if (handle <= 0) {
        return -1;
    }
    for (size_t i = 0; i < slots_.size(); i++) {
        if (slots_[i].def.handle == handle) {
            if (slots_[i].def.system) {
                ESP_LOGI(kTag, "Remove: handle=%" PRIi32 " id='%s' denied (system)", handle, slots_[i].def.id);
                return -4;
            }
            const TrackState &t = slots_[i].track;
            ESP_LOGI(kTag, "Remove: handle=%" PRIi32 " id='%s' x=%.1f y=%.1f", handle, slots_[i].def.id, t.last_pos.x,
                t.last_pos.y);
            slots_[i] = std::move(slots_.back());
            slots_.pop_back();
            return 0;
        }
    }
    return -4;
}

void GestureEngine::on_down(const TouchEvent &event)
{
    touch_active_ = true;
    active_pointer_id_ = event.pointer_id;

    const PointF down = { event.x, event.y };

    ESP_LOGI(kTag, "Down: ptr=%d x=%.1f y=%.1f gestures=%u", event.pointer_id, event.x, event.y, (unsigned)slots_.size());

    for (auto &s : slots_) {
        TrackState &t = s.track;
        reset_track(t);

        t.active = true;
        t.anchor = down;
        t.start_time_ms = event.time_ms;
        t.target_index = 0;
        t.down_pos = down;
        t.last_pos = down;

        const float tol_sq = s.def.tolerance_px * s.def.tolerance_px;
        const PointF first = abs_point(s.def, t, 0);
        const float d0 = dist_sq(down, first);

        if (s.def.fixed && d0 > tol_sq) {
            t.active = false;
            ESP_LOGI(kTag, "  '%s' handle=%" PRIi32 ": inactive (fixed; x=%.1f y=%.1f d0_sq=%.1f tol_sq=%.1f)", s.def.id,
                s.def.handle, event.x, event.y, d0, tol_sq);
            continue;
        }

        t.last_dist_to_target = d0;
        if (d0 <= tol_sq) {
            t.target_index = 1;
            t.approach_armed = false;
            ESP_LOGI(kTag, "  '%s' handle=%" PRIi32 ": reached waypoint 0 on Down (x=%.1f y=%.1f)", s.def.id, s.def.handle,
                event.x, event.y);
            if (t.target_index < s.def.points.size()) {
                t.last_dist_to_target = dist_sq(down, abs_point(s.def, t, t.target_index));
            }
        }
        else {
            ESP_LOGI(kTag, "  '%s' handle=%" PRIi32 ": active (x=%.1f y=%.1f target=0 d0_sq=%.1f tol_sq=%.1f)", s.def.id,
                s.def.handle, event.x, event.y, d0, tol_sq);
        }
    }
}

void GestureEngine::on_move_or_up(const TouchEvent &event)
{
    const PointF pos = { event.x, event.y };

    for (auto &s : slots_) {
        TrackState &t = s.track;
        if (!t.active) {
            continue;
        }

        t.last_pos = pos;

        if (s.def.max_duration_ms != 0) {
            const uint64_t duration = (event.time_ms >= t.start_time_ms) ? (event.time_ms - t.start_time_ms) : 0;
            if (duration > (uint64_t)s.def.max_duration_ms) {
                t.active = false;
                ESP_LOGI(kTag, "  '%s' handle=%" PRIi32 ": inactive (x=%.1f y=%.1f duration %" PRIu64 "ms > max %" PRIu32 "ms)",
                    s.def.id, s.def.handle, event.x, event.y, duration, s.def.max_duration_ms);
                continue;
            }
        }

        const float tol_sq = s.def.tolerance_px * s.def.tolerance_px;
        const float approach_slack_px = (s.def.tolerance_px < 12.0f) ? 2.0f : (s.def.tolerance_px * 0.15f);
        const float approach_slack_sq = approach_slack_px * approach_slack_px;

        // Advance waypoint when within tolerance (skip-friendly across coarse samples).
        while (t.target_index < s.def.points.size()) {
            const PointF target = abs_point(s.def, t, t.target_index);
            const float d = dist_sq(pos, target);
            if (d <= tol_sq) {
                ESP_LOGI(kTag, "  '%s' handle=%" PRIi32 ": reached waypoint %u (x=%.1f y=%.1f)", s.def.id, s.def.handle,
                    (unsigned)t.target_index, event.x, event.y);
                t.target_index++;
                t.consecutive_fail_approach = 0;
                t.consecutive_fail_segment = 0;
                t.max_progress = 0.0f;
                if (t.target_index < s.def.points.size()) {
                    t.last_dist_to_target = dist_sq(pos, abs_point(s.def, t, t.target_index));
                    t.approach_armed = false;
                }
                continue;
            }
            break;
        }

        if (t.target_index >= s.def.points.size()) {
            continue;
        }

        const PointF target = abs_point(s.def, t, t.target_index);
        const float d_to_target = dist_sq(pos, target);

        // Approaching rule:
        // - When switching targets (after reaching a waypoint), allow a brief "pivot" without penalizing distance increases.
        // - Arm the approaching check only after we observe initial progress toward the new target.
        //
        // This avoids false failures at corners where the touch changes direction around the waypoint.
        if (!t.approach_armed && t.target_index > 0) {
            const PointF prev_wp = abs_point(s.def, t, t.target_index - 1);
            if (dist_sq(pos, prev_wp) <= tol_sq) {
                t.last_dist_to_target = d_to_target;
            }
            else if (d_to_target + approach_slack_sq < t.last_dist_to_target) {
                t.approach_armed = true;
                t.consecutive_fail_approach = 0;
                t.last_dist_to_target = d_to_target;
                ESP_LOGI(kTag, "  '%s' handle=%" PRIi32 ": approach armed (x=%.1f y=%.1f target=%u)", s.def.id, s.def.handle,
                    event.x, event.y, (unsigned)t.target_index);
            }
            else {
                t.last_dist_to_target = d_to_target;
            }
        }
        else {
            if (d_to_target > (t.last_dist_to_target + approach_slack_sq)) {
                t.consecutive_fail_approach++;
            }
            else {
                t.consecutive_fail_approach = 0;
            }
            t.last_dist_to_target = d_to_target;
        }

        if (s.def.segment_constraint_enabled && t.target_index > 0) {
            const PointF prev = abs_point(s.def, t, t.target_index - 1);
            const float d_seg = dist_sq_point_to_segment(pos, prev, target);
            if (d_seg > tol_sq) {
                t.consecutive_fail_segment++;
            }
            else {
                t.consecutive_fail_segment = 0;
            }
        }
        else {
            t.consecutive_fail_segment = 0;
        }

        if (t.consecutive_fail_approach >= kConsecutiveFailThreshold
            || t.consecutive_fail_segment >= kConsecutiveFailThreshold) {
            t.active = false;
            ESP_LOGI(kTag,
                "  '%s' handle=%" PRIi32 ": inactive (x=%.1f y=%.1f approach_fail=%d segment_fail=%d target=%u)",
                s.def.id, s.def.handle, event.x, event.y, t.consecutive_fail_approach, t.consecutive_fail_segment,
                (unsigned)t.target_index);
        }
    }
}

int32_t GestureEngine::on_up_and_select_winner(const TouchEvent &event)
{
    const PointF up = { event.x, event.y };

    int32_t best_handle = 0;
    int32_t best_priority = INT32_MIN;
    float best_score = 0.0f;

    ESP_LOGI(kTag, "Up: ptr=%d x=%.1f y=%.1f", event.pointer_id, event.x, event.y);

    for (auto &s : slots_) {
        TrackState &t = s.track;
        if (!t.active) {
            continue;
        }

        if (s.def.points.empty()) {
            continue;
        }

        if (s.def.max_duration_ms != 0) {
            const uint64_t duration = (event.time_ms >= t.start_time_ms) ? (event.time_ms - t.start_time_ms) : 0;
            if (duration > (uint64_t)s.def.max_duration_ms) {
                continue;
            }
        }

        const float tol_sq = s.def.tolerance_px * s.def.tolerance_px;
        const PointF last = abs_point(s.def, t, s.def.points.size() - 1);
        const float score = dist_sq(up, last);

        const bool all_waypoints_reached = (t.target_index >= s.def.points.size());
        const bool up_near_last = (score <= tol_sq);
        if (!all_waypoints_reached || !up_near_last) {
            ESP_LOGI(kTag,
                "  '%s' handle=%" PRIi32 ": not eligible (x=%.1f y=%.1f reached=%d/%u score_sq=%.1f tol_sq=%.1f)",
                s.def.id, s.def.handle, event.x, event.y, (int)t.target_index, (unsigned)s.def.points.size(), score, tol_sq);
            continue;
        }

        ESP_LOGI(kTag, "  '%s' handle=%" PRIi32 ": eligible (x=%.1f y=%.1f pri=%" PRIi32 " score_sq=%.1f)", s.def.id,
            s.def.handle, event.x, event.y, s.def.priority, score);

        if (best_handle == 0 || s.def.priority > best_priority
            || (s.def.priority == best_priority && score < best_score)
            || (s.def.priority == best_priority && score == best_score && s.def.handle < best_handle)) {
            best_handle = s.def.handle;
            best_priority = s.def.priority;
            best_score = score;
        }
    }

    if (best_handle > 0) {
        ESP_LOGI(kTag, "Winner: handle=%" PRIi32 " pri=%" PRIi32 " score_sq=%.1f x=%.1f y=%.1f", best_handle, best_priority,
            best_score, event.x, event.y);
    }
    else {
        ESP_LOGI(kTag, "Winner: none (x=%.1f y=%.1f)", event.x, event.y);
    }

    return best_handle;
}

int32_t GestureEngine::ProcessTouchEvent(const TouchEvent &event)
{
    if (slots_.empty()) {
        return 0;
    }

    switch (event.type) {
        case TouchType::Down:
            on_down(event);
            return 0;
        case TouchType::Move:
            if (!touch_active_ || event.pointer_id != active_pointer_id_) {
                return 0;
            }
            on_move_or_up(event);
            return 0;
        case TouchType::Up: {
            if (!touch_active_ || event.pointer_id != active_pointer_id_) {
                ResetTracking();
                return 0;
            }
            on_move_or_up(event);
            const int32_t winner = on_up_and_select_winner(event);
            ResetTracking();
            return winner;
        }
        case TouchType::Cancel:
        default:
            ResetTracking();
            return 0;
    }
}

GestureEngine &gesture_engine()
{
    static GestureEngine engine;
    return engine;
}
