#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define CONFIG_MAX_NAME_LEN    32
#define CONFIG_MAX_NEOPIXELS   4
#define CONFIG_MAX_ENDSTOPS    8

typedef struct {
    char     name[CONFIG_MAX_NAME_LEN];
    uint8_t  gpio;
    uint32_t count;
} neopixel_cfg_t;

typedef struct {
    char    name[CONFIG_MAX_NAME_LEN];
    uint8_t gpio;
    bool    pull_up;
    bool    active_low;
} endstop_cfg_t;

typedef struct {
    char device_name[CONFIG_MAX_NAME_LEN]; /* friendly name, used in BLE advertising */

    neopixel_cfg_t neopixels[CONFIG_MAX_NEOPIXELS];
    int            neopixel_count;

    endstop_cfg_t  endstops[CONFIG_MAX_ENDSTOPS];
    int            endstop_count;
} device_cfg_t;

/**
 * Mount SPIFFS, read /spiffs/config.json, parse it.
 * On success fills *cfg and returns ESP_OK.
 * Returns ESP_ERR_NOT_FOUND when no config file is present
 * (caller should apply defaults).
 */
esp_err_t config_loader_init(device_cfg_t *cfg);

/** Write *cfg back to /spiffs/config.json (for OTA config push). */
esp_err_t config_loader_save(const device_cfg_t *cfg);
