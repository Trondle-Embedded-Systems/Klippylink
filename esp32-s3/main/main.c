#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "gatt_server.h"

#define TAG "S3_MAIN"

void app_main(void)
{
    ESP_LOGI(TAG, "Klippylink ESP32-S3 starting...");

    gatt_server_init();

    ESP_LOGI(TAG, "GATT server ready. Waiting for BLE connections...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
