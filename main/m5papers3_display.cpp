#include "m5papers3_display.h"
#include "wasm/api/display_lgfx.h"
#include "wasm/api/display_fastepd.h"
#include "driver/gpio.h"
#include "esp_log.h"

static constexpr const char* TAG = "m5papers3_display";

// M5PaperS3: keep PWROFF_PULSE_PIN low during normal operation.
static constexpr gpio_num_t kPwroffPulsePin = GPIO_NUM_44;

LGFX_M5PaperS3::LGFX_M5PaperS3() {
    configure_bus();
    configure_panel();
    configure_touch();
    setPanel(&panel);
    setColorDepth(lgfx::color_depth_t::grayscale_8bit);
}

void LGFX_M5PaperS3::configure_bus() {
    auto bus_cfg = bus.config();
    bus_cfg.bus_speed = 16'000'000;
    bus_cfg.bus_width = 8;
    bus_cfg.pin_d0 = GPIO_NUM_6;
    bus_cfg.pin_d1 = GPIO_NUM_14;
    bus_cfg.pin_d2 = GPIO_NUM_7;
    bus_cfg.pin_d3 = GPIO_NUM_12;
    bus_cfg.pin_d4 = GPIO_NUM_9;
    bus_cfg.pin_d5 = GPIO_NUM_11;
    bus_cfg.pin_d6 = GPIO_NUM_8;
    bus_cfg.pin_d7 = GPIO_NUM_10;
    bus_cfg.pin_pwr = GPIO_NUM_46;
    bus_cfg.pin_spv = GPIO_NUM_17;
    bus_cfg.pin_ckv = GPIO_NUM_18;
    bus_cfg.pin_sph = GPIO_NUM_13;
    bus_cfg.pin_oe = GPIO_NUM_45;
    bus_cfg.pin_le = GPIO_NUM_15;
    bus_cfg.pin_cl = GPIO_NUM_16;
    bus.config(bus_cfg);
  }

void LGFX_M5PaperS3::configure_panel() {
    panel.setBus(&bus);

    auto cfg_detail = panel.config_detail();
    cfg_detail.line_padding = 8;
    panel.config_detail(cfg_detail);

    auto cfg = panel.config();
    cfg.memory_width = 960;
    cfg.panel_width = 960;
    cfg.memory_height = 540;
    cfg.panel_height = 540;
    cfg.offset_rotation = 3;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    cfg.bus_shared = false;
    panel.config(cfg);
  }

  // Configuration values are copied from M5GFX's M5PaperS3 setup:
  // - GT911 on I2C_NUM_1 @ 400kHz
  // - SDA=41, SCL=42, INT=48
  // - x:[0..539], y:[0..959], offset_rotation=1
void LGFX_M5PaperS3::configure_touch() {
    auto cfg = touch.config();
    cfg.pin_sda = GPIO_NUM_41;
    cfg.pin_scl = GPIO_NUM_42;
    cfg.pin_int = GPIO_NUM_48;
    cfg.pin_rst = -1;
    cfg.i2c_port = I2C_NUM_1;
    cfg.freq = 400000;
    // On M5PaperS3 the GT911 is typically on 0x5D; starting with 0x14 causes a
    // harmless NACK that ESP-IDF logs as an error.
    cfg.i2c_addr = lgfx::Touch_GT911::default_addr_2;
    cfg.x_min = 0;
    cfg.x_max = 539;
    cfg.y_min = 0;
    cfg.y_max = 959;
    cfg.offset_rotation = 1;
    cfg.bus_shared = false;
    touch.config(cfg);
    panel.setTouch(&touch);
  }

void hold_pwroff_pulse_low() {
  ESP_LOGI(TAG, "Holding PWROFF_PULSE low (gpio=%d)", static_cast<int>(kPwroffPulsePin));
  gpio_config_t io_conf{};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = 1ULL << static_cast<uint32_t>(kPwroffPulsePin);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
  gpio_set_level(kPwroffPulsePin, 0);
}

LGFX_M5PaperS3 &paper_display() {
  static LGFX_M5PaperS3 display;
  return display;
}

namespace {

void set_display_driver(PaperDisplayDriver driver)
{
  switch (driver) {
  case PaperDisplayDriver::lgfx:
    Display::setCurrent(std::make_unique<DisplayLgfx>());
    break;
  case PaperDisplayDriver::fastepd:
    Display::setCurrent(std::make_unique<DisplayFastEpd>());
    break;
  default:
    Display::setCurrent(std::make_unique<DisplayLgfx>());
    break;
  }
}

bool g_attempted = false;
bool g_ok = false;
PaperDisplayDriver g_current_driver = PaperDisplayDriver::lgfx;

} // namespace

bool paper_display_ensure_init() {
  if (g_attempted) {
    return g_ok;
  }
  (void)paper_display_ensure_init(PaperDisplayDriver::lgfx);
  return g_ok;
}

bool paper_display_ensure_init(PaperDisplayDriver driver) {
  if (g_attempted) {
    if (g_ok && g_current_driver != driver) {
      g_current_driver = driver;
      set_display_driver(driver);
    }
    return g_ok;
  }
  g_attempted = true;

  ESP_LOGI(TAG, "Initializing display (M5PaperS3)...");
  hold_pwroff_pulse_low();
  ESP_LOGI(TAG, "Calling LGFX init()...");
  g_ok = paper_display().init();
  if (!g_ok) {
    ESP_LOGE(TAG, "LGFX init() failed");
    return false;
  }
  ESP_LOGI(TAG, "Display init OK: w=%d h=%d rotation=%d",
      static_cast<int>(paper_display().width()),
      static_cast<int>(paper_display().height()),
      static_cast<int>(paper_display().getRotation()));

  g_current_driver = driver;
  set_display_driver(driver);
  return g_ok;
}
