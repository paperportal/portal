#include "m5papers3_display.h"
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

PaperDisplayDriver g_current_driver = PaperDisplayDriver::fastepd;
bool g_lgfx_touch_ready = false;

bool ensure_lgfx_touch_ready()
{
  if (g_lgfx_touch_ready) {
    return true;
  }

  auto *touch = static_cast<lgfx::LGFX_Device &>(paper_display()).touch();
  if (!touch) {
    ESP_LOGE(TAG, "LGFX touch backend missing");
    return false;
  }

  ESP_LOGI(TAG, "Initializing LGFX touch controller for input polling");
  g_lgfx_touch_ready = touch->init();
  if (!g_lgfx_touch_ready) {
    ESP_LOGE(TAG, "Failed to initialize LGFX touch controller");
    return false;
  }

  // Keep LGFX panel geometry/rotation state initialized for convertRawXY(),
  // but avoid full display init which would claim the i80 bus.
  static_cast<lgfx::LGFX_Device &>(paper_display()).setRotation(0);
  return true;
}

} // namespace

bool paper_display_ensure_init() {
  PaperDisplayDriver driver = Display::current()->driver();
  if (driver == PaperDisplayDriver::none) {
    driver = g_current_driver;
  }
  return paper_display_ensure_init(driver);
}

bool paper_display_ensure_init(PaperDisplayDriver driver) {
  g_current_driver = driver;
  if (driver == PaperDisplayDriver::fastepd) {
    if (!ensure_lgfx_touch_ready()) {
      return false;
    }
  }
  if (Display::current()->driver() != driver) {
      ESP_LOGI(TAG, "Ensuring display initialization for driver=%d", static_cast<int>(driver));
      Display::setCurrent(driver);
      if (!Display::current()->init()) {
        ESP_LOGE(TAG, "Display initialization failed for driver=%d", static_cast<int>(driver));
        return false;
      }
  }
  return true;
}

void paper_touch_set_rotation(uint_fast8_t rot) {
  static_cast<lgfx::LGFX_Device &>(paper_display()).setRotation(rot);
}
