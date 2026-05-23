#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_central.h"
#include "../../shared/protocol/klip_protocol.h"

#define TAG "BLE_CENTRAL"

static ble_response_cb_t s_response_cb = NULL;

void ble_central_init(ble_response_cb_t response_cb)
{
    s_response_cb = response_cb;
    ESP_LOGI(TAG, "BLE central initializing...");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    ESP_LOGI(TAG, "BLE central ready");
}

void ble_central_send(const klip_packet_t *pkt, uint8_t total_len)
{
    ESP_LOGI(TAG, "Sending to S3: cmd=0x%02X len=%d", pkt->command, pkt->length);
    // TODO: Implement GATT write to connected S3 peripheral
}

void ble_central_poll(void)
{
    // TODO: Handle GATT notifications/indications from S3
    // When data arrives, call s_response_cb if set
}
