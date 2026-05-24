#include "led_effects.h"
#include "neopixel.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <math.h>

#define TAG      "LED_FX"
#define TICK_MS  20   /* task period: 50 Hz */

typedef struct {
    led_zone_cfg_t cfg;
    uint32_t       tick;
} zone_state_t;

static zone_state_t     s_zones[LED_MAX_ZONES];
static SemaphoreHandle_t s_mutex;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static inline uint8_t dim(uint8_t v, uint8_t bri)
{
    return (uint8_t)((uint16_t)v * bri / 255);
}

/* Map speed (0–255) to a period in ticks: speed 255 → 1 tick, speed 0 → 50 ticks */
static inline uint32_t speed_period(uint8_t speed)
{
    return 1 + (uint32_t)(255 - speed) * 49 / 255;
}

/* HSV → RGB (all 0–255). Uses integer arithmetic only. */
static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                        uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = *g = *b = v; return; }
    uint8_t region    = h / 43;
    uint8_t remainder = (uint8_t)((h - region * 43) * 6);
    uint8_t p = dim(v, 255 - s);
    uint8_t q = dim(v, 255 - (uint8_t)((uint16_t)s * remainder / 255));
    uint8_t t = dim(v, 255 - (uint8_t)((uint16_t)s * (255 - remainder) / 255));
    switch (region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default:*r = v; *g = p; *b = q; break;
    }
}

/* ── Per-effect renderers (set pixels only, no flush) ───────────────────── */

static void render_zone(zone_state_t *z)
{
    const led_zone_cfg_t *c = &z->cfg;
    uint8_t  bri    = c->brightness;
    uint32_t period = speed_period(c->speed);

    switch (c->effect) {

    case EFFECT_SOLID:
        for (uint8_t i = 0; i < c->count; i++)
            neopixel_set_pixel(c->strip_idx, c->start + i,
                               dim(c->r, bri), dim(c->g, bri), dim(c->b, bri));
        break;

    case EFFECT_BLINK: {
        bool on = (z->tick / period) % 2 == 0;
        for (uint8_t i = 0; i < c->count; i++) {
            if (on)
                neopixel_set_pixel(c->strip_idx, c->start + i,
                                   dim(c->r, bri), dim(c->g, bri), dim(c->b, bri));
            else
                neopixel_set_pixel(c->strip_idx, c->start + i, 0, 0, 0);
        }
        z->tick++;
        break;
    }

    case EFFECT_BREATHE: {
        uint32_t cycle = period * 2;
        float phase = (float)(z->tick % cycle) / (float)cycle;
        /* sinf gives -1..1; map to 0..1 */
        float env = (sinf(phase * 2.0f * 3.14159f) + 1.0f) * 0.5f;
        uint8_t lv = (uint8_t)(env * bri);
        for (uint8_t i = 0; i < c->count; i++)
            neopixel_set_pixel(c->strip_idx, c->start + i,
                               dim(c->r, lv), dim(c->g, lv), dim(c->b, lv));
        z->tick++;
        break;
    }

    case EFFECT_RAINBOW: {
        uint8_t hue_base = (uint8_t)((z->tick / period) % 256);
        for (uint8_t i = 0; i < c->count; i++) {
            uint8_t h = (uint8_t)(hue_base + (uint32_t)i * 256 / c->count);
            uint8_t r, g, b;
            hsv_to_rgb(h, 255, bri, &r, &g, &b);
            neopixel_set_pixel(c->strip_idx, c->start + i, r, g, b);
        }
        z->tick++;
        break;
    }

    case EFFECT_CHASE: {
        uint8_t pos = (uint8_t)((z->tick / period) % c->count);
        for (uint8_t i = 0; i < c->count; i++) {
            if (i == pos)
                neopixel_set_pixel(c->strip_idx, c->start + i,
                                   dim(c->r, bri), dim(c->g, bri), dim(c->b, bri));
            else
                neopixel_set_pixel(c->strip_idx, c->start + i,
                                   dim(c->r2, bri), dim(c->g2, bri), dim(c->b2, bri));
        }
        z->tick++;
        break;
    }

    case EFFECT_WIPE: {
        uint32_t cycle_len = (uint32_t)c->count * 2;
        uint8_t  pos = (uint8_t)((z->tick / period) % cycle_len);
        for (uint8_t i = 0; i < c->count; i++) {
            bool lit = (pos < c->count) ? (i <= pos) : (i > pos - c->count);
            if (lit)
                neopixel_set_pixel(c->strip_idx, c->start + i,
                                   dim(c->r, bri), dim(c->g, bri), dim(c->b, bri));
            else
                neopixel_set_pixel(c->strip_idx, c->start + i, 0, 0, 0);
        }
        z->tick++;
        break;
    }

    case EFFECT_TWINKLE:
        if (z->tick % period == 0) {
            uint8_t idx = (uint8_t)(esp_random() % c->count);
            if (esp_random() & 1)
                neopixel_set_pixel(c->strip_idx, c->start + idx,
                                   dim(c->r, bri), dim(c->g, bri), dim(c->b, bri));
            else
                neopixel_set_pixel(c->strip_idx, c->start + idx,
                                   dim(c->r2, bri), dim(c->g2, bri), dim(c->b2, bri));
        }
        z->tick++;
        break;

    default:
        break;
    }
}

/* ── Effects task ────────────────────────────────────────────────────────── */

static void effects_task(void *arg)
{
    bool dirty[NEOPIXEL_MAX_STRIPS];

    while (1) {
        memset(dirty, 0, sizeof(dirty));

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        for (int i = 0; i < LED_MAX_ZONES; i++) {
            zone_state_t *z = &s_zones[i];
            if (!z->cfg.active) continue;
            render_zone(z);
            if (z->cfg.strip_idx < NEOPIXEL_MAX_STRIPS)
                dirty[z->cfg.strip_idx] = true;
        }
        xSemaphoreGive(s_mutex);

        for (int s = 0; s < NEOPIXEL_MAX_STRIPS; s++) {
            if (dirty[s]) neopixel_flush(s);
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t led_effects_init(void)
{
    memset(s_zones, 0, sizeof(s_zones));
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    xTaskCreate(effects_task, "led_fx", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "Effects engine started (%d zones, %d Hz)", LED_MAX_ZONES, 1000 / TICK_MS);
    return ESP_OK;
}

esp_err_t led_effects_set_zone(uint8_t zone_id, const led_zone_cfg_t *cfg)
{
    if (zone_id >= LED_MAX_ZONES) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_zones[zone_id].cfg  = *cfg;
    s_zones[zone_id].cfg.active = true;
    s_zones[zone_id].tick = 0;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Zone %d: strip=%d start=%d count=%d effect=%d",
             zone_id, cfg->strip_idx, cfg->start, cfg->count, cfg->effect);
    return ESP_OK;
}

esp_err_t led_effects_clear_zone(uint8_t zone_id)
{
    if (zone_id >= LED_MAX_ZONES) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    led_zone_cfg_t *c = &s_zones[zone_id].cfg;
    if (c->active) {
        /* Turn off pixels synchronously before clearing the zone */
        for (uint8_t i = 0; i < c->count; i++)
            neopixel_set_pixel(c->strip_idx, c->start + i, 0, 0, 0);
        neopixel_flush(c->strip_idx);
    }
    memset(&s_zones[zone_id], 0, sizeof(zone_state_t));
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Zone %d cleared", zone_id);
    return ESP_OK;
}
