#include "display_fastepd_arc.h"

#include <math.h>

#include <FastEPD.h>

namespace {

constexpr float kDegToRad = 0.017453292519943295769236907684886f;

void draw_hline_clipped(FASTEPD &epd, int32_t x, int32_t y, int32_t w, uint8_t color)
{
    if (w <= 0) {
        return;
    }

    const int32_t screen_w = (int32_t)epd.width();
    const int32_t screen_h = (int32_t)epd.height();
    if (screen_w <= 0 || screen_h <= 0) {
        return;
    }
    if ((uint32_t)y >= (uint32_t)screen_h) {
        return;
    }

    int32_t x0 = x;
    int32_t x1 = x + w - 1;
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 >= screen_w) {
        x1 = screen_w - 1;
    }
    if (x0 > x1) {
        return;
    }

    epd.drawLine((int)x0, (int)y, (int)x1, (int)y, (int)color);
}

void fill_arc_helper(
    FASTEPD &epd,
    int32_t cx,
    int32_t cy,
    int32_t oradius,
    int32_t iradius,
    float start,
    float end,
    uint8_t color)
{
    float s_cos = cosf(start * kDegToRad);
    float e_cos = cosf(end * kDegToRad);
    float sslope = s_cos / (sinf(start * kDegToRad));
    float eslope = -1000000.0f;
    if (end != 360.0f) {
        eslope = e_cos / (sinf(end * kDegToRad));
    }
    float swidth = 0.5f / s_cos;
    float ewidth = -0.5f / e_cos;

    bool start180 = !(start < 180.0f);
    bool end180 = end < 180.0f;
    bool reversed = start + 180.0f < end || (end < start && start < end + 180.0f);

    int32_t xleft = -oradius;
    int32_t xright = oradius + 1;
    int32_t y = -oradius;
    int32_t ye = oradius;
    if (!reversed) {
        if ((end >= 270.0f || end < 90.0f) && (start >= 270.0f || start < 90.0f)) {
            xleft = 0;
        } else if (end < 270.0f && end >= 90.0f && start < 270.0f && start >= 90.0f) {
            xright = 1;
        }
        if (end >= 180.0f && start >= 180.0f) {
            ye = 0;
        } else if (end < 180.0f && start < 180.0f) {
            y = 0;
        }
    }

    const int32_t screen_w = (int32_t)epd.width();
    const int32_t screen_h = (int32_t)epd.height();

    const int32_t min_y = -cy;
    const int32_t max_y = (screen_h - 1) - cy;
    if (y < min_y) {
        y = min_y;
    }
    if (ye > max_y) {
        ye = max_y;
    }

    const int32_t min_x = -cx;
    const int32_t max_x_exclusive = screen_w - cx;
    if (xleft < min_x) {
        xleft = min_x;
    }
    if (xright > max_x_exclusive) {
        xright = max_x_exclusive;
    }

    const int64_t iradius2_edge = (int64_t)iradius * (int64_t)(iradius - 1);
    const int64_t oradius2_edge = (int64_t)oradius * (int64_t)(oradius + 1);

    for (int32_t yy = y; yy <= ye; ++yy) {
        const int64_t y2 = (int64_t)yy * (int64_t)yy;
        const int64_t compare_o = oradius2_edge - y2;
        const int64_t compare_i = iradius2_edge - y2;

        if (compare_o <= 0) {
            continue;
        }

        int32_t xe = (int32_t)ceilf(sqrtf((float)compare_o));
        int32_t x = 1 - xe;

        if (x < xleft) {
            x = xleft;
        }
        if (xe > xright) {
            xe = xright;
        }

        float ysslope = (yy + swidth) * sslope;
        float yeslope = (yy + ewidth) * eslope;

        int32_t len = 0;
        for (int32_t xx = x; xx <= xe; ++xx) {
            bool flg1 = start180 != (xx <= ysslope);
            bool flg2 = end180 != (xx <= yeslope);

            const int64_t x2 = (int64_t)xx * (int64_t)xx;
            if (x2 >= compare_i
                && ((flg1 && flg2) || (reversed && (flg1 || flg2)))
                && xx != xe
                && x2 < compare_o) {
                ++len;
                continue;
            }

            if (len) {
                draw_hline_clipped(epd, cx + xx - len, cy + yy, len, color);
                len = 0;
            }

            if (x2 >= compare_o) {
                break;
            }

            if (xx < 0 && x2 < compare_i) {
                xx = -xx;
            }
        }
    }
}

} // namespace

void display_fastepd_fill_arc(
    FASTEPD &epd,
    int32_t cx,
    int32_t cy,
    int32_t r0,
    int32_t r1,
    float start_deg,
    float end_deg,
    uint8_t color)
{
    if (r0 <= 0) {
        if (r0 == 0 && r1 == 0 && epd.width() > 0 && epd.height() > 0) {
            if ((uint32_t)cx < (uint32_t)epd.width() && (uint32_t)cy < (uint32_t)epd.height()) {
                epd.drawPixel((int)cx, (int)cy, color);
            }
        }
        return;
    }
    if (r1 < 0 || r1 >= r0) {
        return;
    }

    bool ring = fabsf(start_deg - end_deg) >= 360.0f;
    float start = fmodf(start_deg, 360.0f);
    float end = fmodf(end_deg, 360.0f);
    if (start < 0.0f) {
        start = fmodf(start + 360.0f, 360.0f);
    }
    if (end < 0.0f) {
        end = fmodf(end + 360.0f, 360.0f);
    }
    if (ring && (fabsf(start - end) <= 0.0001f)) {
        start = 0.0f;
        end = 360.0f;
    }

    fill_arc_helper(epd, cx, cy, r0, r1, start, end, color);
}
