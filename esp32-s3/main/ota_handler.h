#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef void (*ota_status_cb_t)(uint8_t status);  /* KLIP_OTA_* constants */

/** Begin receiving a new firmware image. size and crc32 from the BEGIN packet. */
esp_err_t ota_begin(uint32_t size, uint32_t crc32, ota_status_cb_t cb);

/** Feed the next data chunk. Returns ESP_OK while in progress. */
esp_err_t ota_write(const uint8_t *data, size_t len);

/**
 * Finalise the update.
 * Verifies CRC32, commits the partition, and reboots on success.
 * On failure the existing partition is preserved.
 */
esp_err_t ota_end(void);

/** Abort an in-progress update without rebooting. */
void ota_abort(void);
