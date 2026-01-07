#pragma once

#include <stdbool.h>

#include "esp_err.h"

namespace power_service {

// Powers off the device. If show_sleep_image is true, best-effort draws the embedded sleep image first.
esp_err_t power_off(bool show_sleep_image);

} // namespace power_service

