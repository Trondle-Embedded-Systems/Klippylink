#pragma once

#include "esp_err.h"
#include "config_loader.h"
#include <stdint.h>

/**
 * Start the BLE GATT server.
 * Advertises under the device name from *cfg.
 * Handles incoming KlipLink packets and dispatches to neopixel/endstop/ota.
 */
esp_err_t gatt_server_init(const device_cfg_t *cfg);

/** Send a raw klip packet to the connected central (router) via notification. */
esp_err_t gatt_server_notify(const uint8_t *data, uint8_t len);
