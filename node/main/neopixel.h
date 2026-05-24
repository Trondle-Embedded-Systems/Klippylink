#pragma once

#include "esp_err.h"
#include <stdint.h>

#define NEOPIXEL_MAX_STRIPS 4

/**
 * Initialise one WS2812B strip on a GPIO pin using the RMT peripheral.
 * Returns the strip index (0-based) in *strip_idx_out.
 */
esp_err_t neopixel_init(uint8_t gpio_num, uint32_t led_count, int *strip_idx_out);

/** Set a single pixel colour and refresh the strip immediately. */
esp_err_t neopixel_set(int strip_idx, uint32_t led_idx, uint8_t r, uint8_t g, uint8_t b);

/** Set every pixel to the same colour and refresh. */
esp_err_t neopixel_set_all(int strip_idx, uint8_t r, uint8_t g, uint8_t b);

/** Set a contiguous range of pixels and refresh. */
esp_err_t neopixel_set_range(int strip_idx, uint32_t start, uint32_t count,
                              uint8_t r, uint8_t g, uint8_t b);

/** Turn all pixels off. */
esp_err_t neopixel_clear(int strip_idx);

/** Return how many LEDs are on a strip. */
uint32_t neopixel_count(int strip_idx);
