#include "endstop.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"

#define TAG "ENDSTOP"

typedef struct {
    uint8_t  gpio;
    bool     active_low;
    endstop_change_cb_t cb;
} endstop_t;

static endstop_t s_endstops[ENDSTOP_MAX];
static uint8_t   s_count = 0;
static bool      s_isr_service_installed = false;

static void IRAM_ATTR endstop_isr_handler(void *arg)
{
    uint8_t idx = (uint8_t)(uintptr_t)arg;
    if (idx >= s_count) return;
    endstop_t *e = &s_endstops[idx];
    if (e->cb) {
        int level = gpio_get_level(e->gpio);
        bool triggered = e->active_low ? (level == 0) : (level == 1);
        e->cb(idx, triggered);
    }
}

esp_err_t endstop_init(uint8_t gpio_num, bool pull_up, bool active_low,
                       endstop_change_cb_t cb, uint8_t *idx_out)
{
    if (s_count >= ENDSTOP_MAX) {
        ESP_LOGE(TAG, "Max endstops reached");
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = (cb != NULL) ? GPIO_INTR_ANYEDGE : GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) return ret;

    uint8_t idx = s_count;
    s_endstops[idx].gpio       = gpio_num;
    s_endstops[idx].active_low = active_low;
    s_endstops[idx].cb         = cb;

    if (cb != NULL) {
        if (!s_isr_service_installed) {
            gpio_install_isr_service(0);
            s_isr_service_installed = true;
        }
        gpio_isr_handler_add(gpio_num, endstop_isr_handler, (void *)(uintptr_t)idx);
    }

    s_count++;
    *idx_out = idx;
    ESP_LOGI(TAG, "Endstop %d: gpio=%d pull_up=%d active_low=%d",
             idx, gpio_num, pull_up, active_low);
    return ESP_OK;
}

bool endstop_read(uint8_t idx)
{
    if (idx >= s_count) return false;
    endstop_t *e = &s_endstops[idx];
    int level = gpio_get_level(e->gpio);
    return e->active_low ? (level == 0) : (level != 0);
}

uint8_t endstop_gpio(uint8_t idx)
{
    if (idx >= s_count) return 0;
    return s_endstops[idx].gpio;
}

uint8_t endstop_count(void)
{
    return s_count;
}
