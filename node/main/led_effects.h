#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define LED_MAX_ZONES 8

typedef enum {
    EFFECT_SOLID   = 0,
    EFFECT_BLINK   = 1,
    EFFECT_BREATHE = 2,
    EFFECT_RAINBOW = 3,
    EFFECT_CHASE   = 4,
    EFFECT_WIPE    = 5,
    EFFECT_TWINKLE = 6,
} led_effect_id_t;

typedef struct {
    bool            active;
    uint8_t         strip_idx;
    uint8_t         start;
    uint8_t         count;
    led_effect_id_t effect;
    uint8_t         r,  g,  b;   /* primary color   */
    uint8_t         r2, g2, b2;  /* secondary color */
    uint8_t         brightness;  /* 0–255           */
    uint8_t         speed;       /* 0–255           */
} led_zone_cfg_t;

/** Start the effects engine task. Call after all neopixel strips are initialised. */
esp_err_t led_effects_init(void);

/** Set or update a zone. Resets the zone animation to tick 0. */
esp_err_t led_effects_set_zone(uint8_t zone_id, const led_zone_cfg_t *cfg);

/** Clear a zone (turns its LEDs off and removes it). */
esp_err_t led_effects_clear_zone(uint8_t zone_id);
