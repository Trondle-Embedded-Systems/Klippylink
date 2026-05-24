#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/** Perform a self-update from a URL (https recommended). Reboots on success. */
esp_err_t ota_self_update(const char *firmware_url);

/**
 * Push a firmware binary to a node over BLE using KLIPCMD_OTA_* packets.
 * data: full firmware binary in RAM, size: its byte length.
 */
esp_err_t ota_push_to_node(uint16_t conn_id,
                            const uint8_t *data, size_t size);
