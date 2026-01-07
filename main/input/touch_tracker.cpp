#include "input/touch_tracker.h"

#include <string.h>

const lgfx::touch_point_t &TouchTracker::getTouchPointRaw(size_t index) const
{
    return raw[index < detail_count ? index : 0];
}

const TouchDetail &TouchTracker::getDetail(size_t index) const
{
    const size_t raw_index = (index < detail_count) ? index : 0;
    const uint16_t id = raw[raw_index].id;
    return details[id < kMaxPoints ? id : 0];
}

void TouchTracker::update(lgfx::LGFX_Device *gfx, uint32_t msec)
{
    if (!gfx) {
        detail_count = 0;
        return;
    }

    if ((msec - last_msec) <= kMinUpdateMsec) { // Avoid high frequency updates
        if (detail_count == 0) {
            return;
        }
        size_t count = 0;
        for (size_t i = 0; i < kMaxPoints; ++i) {
            count += update_detail(&details[i], msec);
        }
        detail_count = (uint8_t)count;
        return;
    }

    last_msec = msec;
    size_t count = gfx->getTouchRaw(raw, kMaxPoints);
    if (!(count || detail_count)) {
        return;
    }

    uint32_t updated_id = 0;
    if (count) {
        lgfx::touch_point_t tp[kMaxPoints];
        memcpy(tp, raw, sizeof(lgfx::touch_point_t) * count);
        gfx->convertRawXY(tp, (uint_fast8_t)count);
        for (size_t i = 0; i < count; ++i) {
            if (tp[i].id < kMaxPoints) {
                updated_id |= (uint32_t)1U << tp[i].id;
                update_detail(&details[tp[i].id], msec, true, &tp[i]);
            }
        }
    }

    for (size_t i = 0; i < kMaxPoints; ++i) {
        if ((!(updated_id & ((uint32_t)1U << i))) && update_detail(&details[i], msec, false, nullptr)
            && (count < kMaxPoints)) {
            ++count;
        }
    }

    detail_count = (uint8_t)count;
}

bool TouchTracker::update_detail(TouchDetail *det, uint32_t msec, bool pressed, const lgfx::touch_point_t *tp)
{
    touch_state_t tm = (touch_state_t)det->state;
    if (tm == touch_state_t::none && !pressed) {
        return false;
    }
    tm = static_cast<touch_state_t>(tm & ~touch_state_t::mask_change);

    if (pressed) {
        det->prev_x = det->x;
        det->prev_y = det->y;
        det->size = tp->size;
        det->id = tp->id;

        if (!(tm & touch_state_t::mask_moving)) { // Processing when not flicked.
            if (tm & touch_state_t::mask_touch) { // Not immediately after the touch.
                const int32_t dx = abs_i32((int32_t)det->base_x - (int32_t)tp->x);
                const int32_t dy = abs_i32((int32_t)det->base_y - (int32_t)tp->y);
                if (dx > flick_thresh || dy > flick_thresh) {
                    det->prev_x = det->base_x;
                    det->prev_y = det->base_y;
                    tm = static_cast<touch_state_t>(tm | touch_state_t::flick_begin);
                }
                else if ((tm == touch_state_t::touch) && (msec - det->base_msec > hold_msec)) {
                    tm = touch_state_t::hold_begin;
                }
            }
            else {
                det->x = tp->x;
                det->y = tp->y;
                det->size = tp->size;
                det->id = tp->id;
                tm = touch_state_t::touch_begin;

                // NOTE: Reset click count for long pauses or if the new touch is far from the prior base.
                const int32_t reset_thresh = (flick_thresh + 1) << 2;
                const int32_t dx = abs_i32((int32_t)det->base_x - (int32_t)tp->x);
                const int32_t dy = abs_i32((int32_t)det->base_y - (int32_t)tp->y);
                if ((msec - det->base_msec > hold_msec) || dx > reset_thresh || dy > reset_thresh) {
                    det->click_count = 0;
                }

                det->base_msec = msec;
                det->base_x = tp->x;
                det->base_y = tp->y;
                det->prev_x = det->base_x;
                det->prev_y = det->base_y;
            }
        }

        if (tm & touch_state_t::mask_moving) {
            det->x = tp->x;
            det->y = tp->y;
        }
    }
    else {
        tm = (tm & touch_state_t::mask_touch)
            ? static_cast<touch_state_t>((tm | touch_state_t::mask_change) & ~touch_state_t::mask_touch)
            : touch_state_t::none;

        if (tm == touch_state_t::touch_end) {
            // Update base_msec for continuous-tap detection.
            det->base_msec = msec;
            det->click_count++;
        }
    }

    det->state = (uint8_t)tm;
    det->_pad = 0;
    return true;
}

bool TouchTracker::update_detail(TouchDetail *det, uint32_t msec)
{
    touch_state_t tm = (touch_state_t)det->state;
    if (tm == touch_state_t::none) {
        return false;
    }

    // Don't clear mask_change for touch_end since they share the same bit (0x02)
    if (tm != touch_state_t::touch_end) {
        tm = static_cast<touch_state_t>(tm & ~touch_state_t::mask_change);
    }

    if (tm & touch_state_t::touch) {
        det->prev_x = det->x;
        det->prev_y = det->y;
        if ((tm == touch_state_t::touch) && (msec - det->base_msec > hold_msec)) {
            tm = touch_state_t::hold_begin;
        }
    }
    else if (tm == touch_state_t::touch_end) {
        // Keep touch_end state until a new touch begins (handled by the other update_detail)
        // Don't clear to none - let it persist so WASM can read it
    }
    else {
        tm = touch_state_t::none;
    }

    det->state = (uint8_t)tm;
    det->_pad = 0;
    return true;
}

TouchTracker &touch_tracker()
{
    static TouchTracker tracker;
    return tracker;
}
