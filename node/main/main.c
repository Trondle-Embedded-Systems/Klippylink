#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config_loader.h"
#include "neopixel.h"
#include "led_effects.h"
#include "endstop.h"
#include "gatt_server.h"

#define TAG "NODE_MAIN"

static device_cfg_t s_cfg;

static void endstop_event_cb(uint8_t idx, bool triggered)
{
    /* Called from ISR; gatt_server_notify is safe from ISR via BLE stack queue */
    ESP_EARLY_LOGI(TAG, "Endstop %d -> %s", idx, triggered ? "TRIGGERED" : "open");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Klippylink node starting...");

    /* Load config from SPIFFS; defaults applied on ESP_ERR_NOT_FOUND */
    esp_err_t ret = config_loader_init(&s_cfg);
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Using default config (no config.json)");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Config load failed: %s", esp_err_to_name(ret));
    }

    /* Initialise NeoPixel strips */
    for (int i = 0; i < s_cfg.neopixel_count; i++) {
        int strip_idx;
        ret = neopixel_init(s_cfg.neopixels[i].gpio,
                            s_cfg.neopixels[i].count,
                            &strip_idx);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "NeoPixel '%s' ready (strip %d)",
                     s_cfg.neopixels[i].name, strip_idx);
        } else {
            ESP_LOGE(TAG, "NeoPixel '%s' init failed: %s",
                     s_cfg.neopixels[i].name, esp_err_to_name(ret));
        }
    }

    /* Start effects engine (must be after neopixel_init calls) */
    if (s_cfg.neopixel_count > 0)
        ESP_ERROR_CHECK(led_effects_init());

    /* Initialise endstops */
    for (int i = 0; i < s_cfg.endstop_count; i++) {
        uint8_t idx;
        ret = endstop_init(s_cfg.endstops[i].gpio,
                           s_cfg.endstops[i].pull_up,
                           s_cfg.endstops[i].active_low,
                           endstop_event_cb,
                           &idx);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Endstop '%s' ready (idx %d)",
                     s_cfg.endstops[i].name, idx);
        } else {
            ESP_LOGE(TAG, "Endstop '%s' init failed: %s",
                     s_cfg.endstops[i].name, esp_err_to_name(ret));
        }
    }

    /* Start BLE GATT server */
    ESP_ERROR_CHECK(gatt_server_init(&s_cfg));

    ESP_LOGI(TAG, "Node ready — advertising as '%s'", s_cfg.device_name);

    /* Endstop polling loop (supplement to ISR for reliability) */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
