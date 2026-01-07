#pragma once

#include <stdint.h>
#include <stddef.h>

#include "m5papers3_display.h"

struct TouchPointRaw
{
    int16_t x;
    int16_t y;
    uint16_t size;
    uint16_t id;
};

struct TouchDetail
{
    int16_t x;
    int16_t y;
    uint16_t size;
    uint16_t id;

    int16_t prev_x;
    int16_t prev_y;
    int16_t base_x;
    int16_t base_y;

    uint32_t base_msec;
    uint8_t state;
    uint8_t click_count;
    uint16_t _pad;
};

static_assert(sizeof(TouchPointRaw) == 8, "TouchPointRaw layout must stay stable");
static_assert(sizeof(TouchDetail) == 24, "TouchDetail layout must stay stable");

// NOTE: Touch state semantics and numeric values match M5Unified's m5::touch_state_t.
enum touch_state_t : uint8_t {
    none = 0b0000,
    touch = 0b0001,
    touch_end = 0b0010,
    touch_begin = 0b0011,

    hold = 0b0101,
    hold_end = 0b0110,
    hold_begin = 0b0111,

    flick = 0b1001,
    flick_end = 0b1010,
    flick_begin = 0b1011,

    drag = 0b1101,
    drag_end = 0b1110,
    drag_begin = 0b1111,

    mask_touch = 0b0001,
    mask_change = 0b0010,
    mask_holding = 0b0100,
    mask_moving = 0b1000,
};

class TouchTracker {
public:
    static constexpr size_t kMaxPoints = 5;
    static constexpr uint32_t kMinUpdateMsec = 4;

    void setHoldThresh(uint16_t msec) { hold_msec = msec; }
    void setFlickThresh(uint16_t distance) { flick_thresh = distance; }

    uint8_t getCount(void) const { return detail_count; }

    const lgfx::touch_point_t &getTouchPointRaw(size_t index = 0) const;
    const TouchDetail &getDetail(size_t index = 0) const;

    void update(lgfx::LGFX_Device *gfx, uint32_t msec);

private:
    static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

    bool update_detail(TouchDetail *det, uint32_t msec, bool pressed, const lgfx::touch_point_t *tp);
    bool update_detail(TouchDetail *det, uint32_t msec);

    uint32_t last_msec = 0;
    int32_t flick_thresh = 8;
    uint32_t hold_msec = 500;

    TouchDetail details[kMaxPoints]{};
    lgfx::touch_point_t raw[kMaxPoints]{};
    uint8_t detail_count = 0;
};

TouchTracker &touch_tracker();
