#include "heater.h"

#ifdef CONFIG_KLIPNODE_VARIANT_FULL

#include "gatt_server.h"
#include "../../shared/protocol/klip_protocol.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#define TAG "HEATER"

/* ADC / thermistor */
#define HEATER_ADC_UNIT     ADC_UNIT_1
#define HEATER_ADC_CHAN     ADC_CHANNEL_0
#define HEATER_ADC_ATTEN    ADC_ATTEN_DB_12

/* Steinhart-Hart coefficients for a generic 100k NTC (10k beta 3950) */
#define THERMISTOR_NOMINAL  10000.0f
#define TEMPERATURE_NOMINAL 25.0f
#define B_COEFFICIENT       3950.0f
#define SERIES_RESISTOR     10000.0f

/* LEDC */
#define HEATER_LEDC_TIMER   LEDC_TIMER_0
#define HEATER_LEDC_CHAN    LEDC_CHANNEL_0
#define HEATER_LEDC_GPIO    5
#define HEATER_LEDC_FREQ_HZ 1000
#define HEATER_LEDC_RES     LEDC_TIMER_8_BIT   /* 0–255 */

#define TASK_HZ             10
#define NOTIFY_INTERVAL_MS  500

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static float s_target  = 0.0f;
static float s_kp      = 1.0f;
static float s_ki      = 0.1f;
static float s_kd      = 0.01f;
static volatile bool s_emergency = false;

static float read_temperature(void)
{
    int raw = 0;
    adc_oneshot_read(s_adc_handle, HEATER_ADC_CHAN, &raw);
    /* Convert raw ADC → resistance → temperature via Steinhart-Hart. */
    float voltage = (float)raw / 4095.0f;
    if (voltage <= 0.0f || voltage >= 1.0f) return -1.0f;
    float resistance = SERIES_RESISTOR * voltage / (1.0f - voltage);
    float steinhart = resistance / THERMISTOR_NOMINAL;
    steinhart = logf(steinhart);
    steinhart /= B_COEFFICIENT;
    steinhart += 1.0f / (TEMPERATURE_NOMINAL + 273.15f);
    steinhart = 1.0f / steinhart;
    return steinhart - 273.15f;
}

static void set_duty(uint8_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, HEATER_LEDC_CHAN, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, HEATER_LEDC_CHAN);
}

static void heater_task(void *arg)
{
    const float dt = 1.0f / TASK_HZ;
    float integral = 0.0f;
    float prev_err = 0.0f;
    TickType_t last_notify = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000 / TASK_HZ));

        if (s_emergency) {
            set_duty(0);
            continue;
        }

        float temp = read_temperature();
        float err  = s_target - temp;
        integral  += err * dt;
        float deriv = (err - prev_err) / dt;
        float out   = s_kp * err + s_ki * integral + s_kd * deriv;
        prev_err    = err;

        uint8_t duty = (uint8_t)(out < 0.0f ? 0 : out > 255.0f ? 255 : out);
        set_duty(duty);

        /* Notify host at NOTIFY_INTERVAL_MS. */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_notify) * portTICK_PERIOD_MS >= NOTIFY_INTERVAL_MS) {
            last_notify = now;
            uint8_t buf[KLIP_HEADER_SIZE + sizeof(klip_heater_state_t)];
            klip_packet_t *np = (klip_packet_t *)buf;
            np->magic   = KLIPPROTO_MAGIC;
            np->command = KLIPCMD_HEATER_STATE;
            np->length  = sizeof(klip_heater_state_t);
            klip_heater_state_t *st = (klip_heater_state_t *)np->payload;
            st->temp_current = temp;
            st->temp_target  = s_target;
            st->duty         = duty;
            gatt_server_notify(buf, sizeof(buf));
        }
    }
}

void heater_init(void)
{
    /* ADC */
    adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = HEATER_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &s_adc_handle));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = HEATER_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle,
                                               HEATER_ADC_CHAN, &chan_cfg));

    /* LEDC */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = HEATER_LEDC_TIMER,
        .duty_resolution = HEATER_LEDC_RES,
        .freq_hz         = HEATER_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t chan = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = HEATER_LEDC_CHAN,
        .timer_sel  = HEATER_LEDC_TIMER,
        .gpio_num   = HEATER_LEDC_GPIO,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan));

    xTaskCreate(heater_task, "heater", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Heater PID task started (GPIO %d)", HEATER_LEDC_GPIO);
}

void heater_set_target(float temp_c)
{
    s_target    = temp_c;
    s_emergency = false;
    ESP_LOGI(TAG, "Target: %.1f °C", temp_c);
}

void heater_set_pid(float kp, float ki, float kd)
{
    s_kp = kp; s_ki = ki; s_kd = kd;
    ESP_LOGI(TAG, "PID: kp=%.3f ki=%.3f kd=%.3f", kp, ki, kd);
}

void heater_emergency_off(void)
{
    s_emergency = true;
    set_duty(0);
    ESP_LOGW(TAG, "Emergency off — duty zeroed");
}

#endif /* CONFIG_KLIPNODE_VARIANT_FULL */
