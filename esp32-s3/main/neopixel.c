#include "neopixel.h"
#include "led_strip.h"
#include "esp_log.h"
#include <string.h>

#define TAG "NEOPIXEL"

typedef struct {
    led_strip_handle_t handle;
    uint32_t count;
} strip_t;

static strip_t s_strips[NEOPIXEL_MAX_STRIPS];
static int s_count = 0;

esp_err_t neopixel_init(uint8_t gpio_num, uint32_t led_count, int *strip_idx_out)
{
    if (s_count >= NEOPIXEL_MAX_STRIPS) {
        ESP_LOGE(TAG, "Max strips reached");
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = gpio_num,
        .max_leds         = led_count,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10 * 1000 * 1000, /* 10 MHz — standard for WS2812B */
        .flags.with_dma = false,
    };

    strip_t *s = &s_strips[s_count];
    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s->count = led_count;
    led_strip_clear(s->handle);

    *strip_idx_out = s_count++;
    ESP_LOGI(TAG, "Strip %d: gpio=%d count=%lu", *strip_idx_out, gpio_num, (unsigned long)led_count);
    return ESP_OK;
}

esp_err_t neopixel_set(int strip_idx, uint32_t led_idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (strip_idx < 0 || strip_idx >= s_count) return ESP_ERR_INVALID_ARG;
    strip_t *s = &s_strips[strip_idx];
    if (led_idx >= s->count) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = led_strip_set_pixel(s->handle, led_idx, r, g, b);
    if (ret != ESP_OK) return ret;
    return led_strip_refresh(s->handle);
}

esp_err_t neopixel_set_all(int strip_idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (strip_idx < 0 || strip_idx >= s_count) return ESP_ERR_INVALID_ARG;
    strip_t *s = &s_strips[strip_idx];

    for (uint32_t i = 0; i < s->count; i++) {
        esp_err_t ret = led_strip_set_pixel(s->handle, i, r, g, b);
        if (ret != ESP_OK) return ret;
    }
    return led_strip_refresh(s->handle);
}

esp_err_t neopixel_set_range(int strip_idx, uint32_t start, uint32_t count,
                              uint8_t r, uint8_t g, uint8_t b)
{
    if (strip_idx < 0 || strip_idx >= s_count) return ESP_ERR_INVALID_ARG;
    strip_t *s = &s_strips[strip_idx];
    if (start >= s->count) return ESP_ERR_INVALID_ARG;

    uint32_t end = start + count;
    if (end > s->count) end = s->count;

    for (uint32_t i = start; i < end; i++) {
        esp_err_t ret = led_strip_set_pixel(s->handle, i, r, g, b);
        if (ret != ESP_OK) return ret;
    }
    return led_strip_refresh(s->handle);
}

esp_err_t neopixel_clear(int strip_idx)
{
    if (strip_idx < 0 || strip_idx >= s_count) return ESP_ERR_INVALID_ARG;
    return led_strip_clear(s_strips[strip_idx].handle);
}

uint32_t neopixel_count(int strip_idx)
{
    if (strip_idx < 0 || strip_idx >= s_count) return 0;
    return s_strips[strip_idx].count;
}
