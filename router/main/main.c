#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "serial_bridge.h"
#include "ble_central.h"
#include "node_registry.h"
#include "klip_protocol.h"

#define TAG "ROUTER_MAIN"

static void on_node_packet(uint16_t conn_id,
                            const klip_packet_t *pkt, uint8_t total_len)
{
    (void)conn_id; (void)pkt; (void)total_len;
    /* Raw forwarding is handled inside ble_central (serial_bridge_emit).
     * Place any additional router-level dispatch here if needed. */
}

void app_main(void)
{
    ESP_LOGI(TAG, "Klippylink router starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    node_registry_init();
    serial_bridge_init();
    ESP_ERROR_CHECK(ble_central_init(on_node_packet));

    ESP_LOGI(TAG, "Router ready — serial bridge active");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Nodes connected: %d", node_count_connected());
    }
}
