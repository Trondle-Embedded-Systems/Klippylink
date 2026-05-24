#include "ota_handler.h"
#include "ble_central.h"
#include "klip_protocol.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "OTA_ROUTER"

esp_err_t ota_self_update(const char *firmware_url)
{
    ESP_LOGI(TAG, "Self-OTA from %s", firmware_url);
    esp_http_client_config_t http_cfg = {
        .url             = firmware_url,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_cfg = {.http_config = &http_cfg};
    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA OK — rebooting");
        esp_restart();
    }
    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t ota_push_to_node(uint16_t conn_id,
                            const uint8_t *data, size_t size)
{
    if (!data || size == 0) return ESP_ERR_INVALID_ARG;

    /* Compute CRC32 */
    uint32_t crc = esp_rom_crc32_le(0, data, size);

    /* Send OTA_BEGIN */
    uint8_t buf[KLIP_MAX_PACKET_SIZE];
    klip_packet_t *pkt = (klip_packet_t *)buf;
    pkt->magic   = KLIPPROTO_MAGIC;
    pkt->command = KLIPCMD_OTA_BEGIN;
    pkt->length  = sizeof(klip_ota_begin_t);
    klip_ota_begin_t *begin = (klip_ota_begin_t *)pkt->payload;
    begin->size  = (uint32_t)size;
    begin->crc32 = crc;
    esp_err_t ret = ble_central_send_to_node(conn_id, buf, KLIP_HEADER_SIZE + pkt->length);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Stream OTA_DATA chunks */
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = size - offset;
        if (chunk > KLIP_MAX_PAYLOAD_SIZE) chunk = KLIP_MAX_PAYLOAD_SIZE;

        pkt->command = KLIPCMD_OTA_DATA;
        pkt->length  = (uint8_t)chunk;
        memcpy(pkt->payload, data + offset, chunk);

        ret = ble_central_send_to_node(conn_id, buf, KLIP_HEADER_SIZE + chunk);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA data send failed at offset %d", (int)offset);
            return ret;
        }
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(20)); /* give node time to write flash */
    }

    /* Send OTA_END */
    pkt->command = KLIPCMD_OTA_END;
    pkt->length  = 0;
    ret = ble_central_send_to_node(conn_id, buf, KLIP_HEADER_SIZE);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "OTA push complete: %d bytes, crc=0x%08lX",
             (int)size, (unsigned long)crc);
    return ESP_OK;
}
