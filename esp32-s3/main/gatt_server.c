#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_device.h"

#include "gatt_server.h"
#include "../../shared/protocol/klip_protocol.h"

#define TAG "GATT_SERVER"

#define GATTS_SERVICE_UUID       0x4B4C
#define GATTS_CHAR_CMD_UUID      0x4C01
#define GATTS_CHAR_TELEMETRY_UUID 0x4C02

enum {
    GATTS_IDX_CMD,
    GATTS_IDX_TELEMETRY,
    GATTS_IDX_COUNT,
};

static uint8_t char_cmd_value[KLIP_MAX_PAYLOAD_SIZE];
static uint8_t char_telemetry_value[KLIP_MAX_PAYLOAD_SIZE];

static esp_gatt_char_prop_t char_property = ESP_GATT_CHAR_PROP_BIT_READ
    | ESP_GATT_CHAR_PROP_BIT_WRITE
    | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT server registered");
        break;
    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle != 0) {
            ESP_LOGI(TAG, "Write event, len=%d", param->write.len);
            memcpy(char_cmd_value, param->write.value, param->write.len);
        }
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(TAG, "BLE client connected");
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "BLE client disconnected, restarting advertising");
        esp_ble_gap_start_advertising(NULL);
        break;
    default:
        break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(NULL);
        break;
    default:
        break;
    }
}

void gatt_server_init(void)
{
    ESP_LOGI(TAG, "Initializing GATT server...");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);

    ESP_LOGI(TAG, "GATT server initialized");
}
