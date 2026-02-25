idf_component_register(
  SRCS
    "main.cpp"
    "m5papers3_display.cpp"
    "sd_card.cpp"
    "host/event_loop.cpp"
    "host/microtask_scheduler.cpp"
    "input/gesture_engine.cpp"
    "input/touch_tracker.cpp"
    "services/devserver_service.cpp"
    "services/power_service.cpp"
    "services/wifi_service.cpp"
    "other/mem_utils.cpp"
    "other/fastepd_xtc.cpp"
    "other/lovyangfx_xtc.cpp"
    "services/settings_service.cpp"
    "wasm/api/core.cpp"
    "wasm/api/devserver.cpp"
    "wasm/api/display.cpp"
    "wasm/api/display_fastepd.cpp"
    "wasm/api/display_fastepd_arc.cpp"
    "wasm/api/display_lgfx.cpp"
    "wasm/api/display.cpp"
    "wasm/api/display_images.cpp"
    "wasm/api/display_primitives.cpp"
    "wasm/api/display_text.cpp"
    "wasm/api/fs.cpp"
    "wasm/api/gesture.cpp"
    "wasm/api/hal.cpp"
    "wasm/api/http.cpp"
    "wasm/api/httpd.cpp"
    "wasm/api/i2c_bus.cpp"
    "wasm/api/imu.cpp"
    "wasm/api/log.cpp"
    "wasm/api/m5.cpp"
    "wasm/api/microtask.cpp"
    "wasm/api/net.cpp"
    "wasm/api/nvs.cpp"
    "wasm/api/power.cpp"
    "wasm/api/rtc.cpp"
    "wasm/api/socket.cpp"
    "wasm/api/speaker.cpp"
    "wasm/api/touch.cpp"
    "wasm/wasm_controller_globals.cpp"
    "wasm/wasm_controller_runtime.cpp"
    "wasm/wasm_controller_load.cpp"
    "wasm/wasm_controller_instance.cpp"
    "wasm/wasm_controller_dispatch.cpp"
    "wasm/wasm_controller_memory.cpp"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_adc esp_http_client esp_http_server esp_netif esp_psram esp_wifi mdns FastEPD LovyanGFX fatfs sdmmc wamr json jpegdec
)

target_add_binary_data(${COMPONENT_LIB} "assets/entrypoint.wasm" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/settings.wasm" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_32.vlw" BINARY)

# Inter system font
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_8.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_9.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_10.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_11.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_12.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_13.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_14.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_15.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_16.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_18.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_20.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_22.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_24.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_26.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_28.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_30.bbf" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/inter_medium_32.bbf" BINARY)

target_add_binary_data(${COMPONENT_LIB} "assets/montserrat_light_20.vlw" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/sleepimage.jpg" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/sleepimage.png" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/wakeimage.jpg" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/icon_battery.png" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/icon_devserver.png" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/icon_softap.png" BINARY)
target_add_binary_data(${COMPONENT_LIB} "assets/icon_wifi.png" BINARY)
