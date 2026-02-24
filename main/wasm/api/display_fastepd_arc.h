#pragma once

#include <stdint.h>

class FASTEPD;

void display_fastepd_fill_arc(
    FASTEPD &epd,
    int32_t cx,
    int32_t cy,
    int32_t r0,
    int32_t r1,
    float start_deg,
    float end_deg,
    uint8_t color);

