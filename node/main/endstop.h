#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define ENDSTOP_MAX 8

typedef void (*endstop_change_cb_t)(uint8_t idx, bool triggered);

/**
 * Initialise one endstop input.
 * pull_up:     enable internal pull-up resistor
 * active_low:  endstop reads LOW when triggered
 * cb:          optional callback fired from ISR context on any edge
 * Returns the endstop index in *idx_out.
 */
esp_err_t endstop_init(uint8_t gpio_num, bool pull_up, bool active_low,
                       endstop_change_cb_t cb, uint8_t *idx_out);

/** Read the current logical state (true = triggered). */
bool endstop_read(uint8_t idx);

/** Return the GPIO pin assigned to an endstop. */
uint8_t endstop_gpio(uint8_t idx);

/** Return how many endstops have been registered. */
uint8_t endstop_count(void);
