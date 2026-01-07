#pragma once

#include <stdbool.h>

#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mount point used by the firmware for the SD card FAT filesystem.
const char *sd_card_mount_point(void);

// Mount the SD card filesystem (idempotent). Returns true on success.
bool sd_card_mount(void);

// Unmount the SD card filesystem (idempotent).
void sd_card_unmount(void);

// Returns true if the SD card filesystem is mounted.
bool sd_card_is_mounted(void);

// Returns the SD card descriptor (or null if unavailable).
const sdmmc_card_t *sd_card_get_card(void);

#ifdef __cplusplus
} // extern "C"
#endif
