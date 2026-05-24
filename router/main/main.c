#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "http_server.h"
#include "ble_central.h"
#include "node_registry.h"
#include "klip_protocol.h"

#define TAG "ROUTER_MAIN"

static void on_node_packet(uint16_t conn_id,
                            const klip_packet_t *pkt, uint8_t total_len)
{
    ESP_LOGD(TAG, "Packet from conn_id=%d cmd=0x%02X len=%d",
             conn_id, pkt->command, pkt->length);
    /* Dispatching is done inside ble_central itself for known packet types.
     * Place custom forwarding logic here if needed. */
}

void app_main(void)
{
    ESP_LOGI(TAG, "Klippylink router starting...");

    /* NVS is required by WiFi and BLE */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Node registry must be ready before BLE central starts */
    node_registry_init();

    /* Connect to WiFi (reads credentials from NVS) */
    ret = wifi_manager_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected: %s", wifi_manager_ip());
        ESP_ERROR_CHECK(http_server_start());
        ESP_LOGI(TAG, "HTTP API on http://%s:8765", wifi_manager_ip());
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "No WiFi credentials — HTTP server not started");
        ESP_LOGW(TAG, "POST /api/wifi with {ssid,password} once connected via USB");
    } else {
        ESP_LOGE(TAG, "WiFi failed: %s — HTTP server not started", esp_err_to_name(ret));
    }

    /* Start BLE central — will scan and auto-connect to KlipLink nodes */
    ESP_ERROR_CHECK(ble_central_init(on_node_packet));

    ESP_LOGI(TAG, "Router ready");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Nodes connected: %d  WiFi: %s",
                 node_count(),
                 wifi_manager_is_connected() ? wifi_manager_ip() : "offline");
    }
}
