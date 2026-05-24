#include "ota_handler.h"
#include "klip_protocol.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define TAG "OTA"

static esp_ota_handle_t       s_handle         = 0;
static const esp_partition_t *s_part           = NULL;
static uint32_t               s_expected_crc   = 0;
static uint32_t               s_received_bytes = 0;
static uint32_t               s_total_bytes    = 0;
static ota_status_cb_t        s_cb             = NULL;
static bool                   s_active         = false;

esp_err_t ota_begin(uint32_t size, uint32_t crc32, ota_status_cb_t cb)
{
    if (s_active) ota_abort();

    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(ret));
        return ret;
    }

    s_expected_crc   = crc32;
    s_total_bytes    = size;
    s_received_bytes = 0;
    s_cb             = cb;
    s_active         = true;

    ESP_LOGI(TAG, "OTA begin: %lu bytes, crc32=0x%08lX",
             (unsigned long)size, (unsigned long)crc32);
    if (s_cb) s_cb(KLIP_OTA_IN_PROGRESS);
    return ESP_OK;
}

esp_err_t ota_write(const uint8_t *data, size_t len)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;

    esp_err_t ret = esp_ota_write(s_handle, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(ret));
        ota_abort();
        if (s_cb) s_cb(KLIP_OTA_ERROR);
        return ret;
    }

    s_received_bytes += len;
    ESP_LOGD(TAG, "OTA progress: %lu/%lu",
             (unsigned long)s_received_bytes, (unsigned long)s_total_bytes);
    return ESP_OK;
}

esp_err_t ota_end(void)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;

    /* Read-back CRC32 verification (skip if crc32 == 0) */
    if (s_expected_crc != 0) {
        uint32_t actual_crc = 0;
        uint8_t *buf = malloc(256);
        if (buf) {
            uint32_t offset = 0;
            while (offset < s_received_bytes) {
                size_t chunk = (s_received_bytes - offset < 256)
                               ? (s_received_bytes - offset) : 256;
                esp_partition_read(s_part, offset, buf, chunk);
                actual_crc = esp_rom_crc32_le(actual_crc, buf, chunk);
                offset += (uint32_t)chunk;
            }
            free(buf);
        }

        if (actual_crc != s_expected_crc) {
            ESP_LOGE(TAG, "CRC mismatch: expected=0x%08lX actual=0x%08lX",
                     (unsigned long)s_expected_crc, (unsigned long)actual_crc);
            esp_ota_abort(s_handle);
            s_active = false;
            if (s_cb) s_cb(KLIP_OTA_ERROR);
            return ESP_ERR_INVALID_CRC;
        }
    }

    esp_err_t ret = esp_ota_end(s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(ret));
        s_active = false;
        if (s_cb) s_cb(KLIP_OTA_ERROR);
        return ret;
    }

    ret = esp_ota_set_boot_partition(s_part);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(ret));
        s_active = false;
        if (s_cb) s_cb(KLIP_OTA_ERROR);
        return ret;
    }

    s_active = false;
    ESP_LOGI(TAG, "OTA success — rebooting");
    if (s_cb) s_cb(KLIP_OTA_OK);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

void ota_abort(void)
{
    if (!s_active) return;
    esp_ota_abort(s_handle);
    s_active = false;
    ESP_LOGW(TAG, "OTA aborted");
}
