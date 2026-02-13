#pragma once

#include "wasm/api/display.h"
#include "LovyanGFX.hpp"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

class LGFX_M5PaperS3 final : public lgfx::LGFX_Device {
 public:
  LGFX_M5PaperS3();

  private:
   // Display (EPD) configuration
   void configure_bus();
   void configure_panel();

   // Touch configuration
   void configure_touch();

   lgfx::Bus_EPD bus{};
   lgfx::Panel_EPD panel{};
   lgfx::Touch_GT911 touch{};
};

struct TouchUiState {
  bool is_down = false;
  int32_t last_x = -1;
  int32_t last_y = -1;
  uint32_t last_render_msec = 0;
};

struct TouchSample {
  bool is_down = false;
  int32_t x = -1;
  int32_t y = -1;
};

struct UiRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;
};

void hold_pwroff_pulse_low();

LGFX_M5PaperS3 &paper_display();
bool paper_display_ensure_init();
bool paper_display_ensure_init(PaperDisplayDriver driver);
void paper_touch_set_rotation(uint_fast8_t rot);
