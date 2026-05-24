#include "serial_bridge.h"
#include "command_parser.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG         "SERIAL_BRIDGE"
#define UART_PORT   UART_NUM_0
#define BAUD_RATE   115200
#define RX_BUF_SIZE 4096
#define TX_BUF_SIZE 0      /* synchronous TX */

static SemaphoreHandle_t s_tx_mutex = NULL;

static void rx_task(void *arg)
{
    uint8_t buf[256];
    while (1) {
        int len = uart_read_bytes(UART_PORT, buf, sizeof(buf),
                                  pdMS_TO_TICKS(20));
        if (len > 0)
            command_parser_feed(buf, (size_t)len);
    }
}

void serial_bridge_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, RX_BUF_SIZE,
                                        TX_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));

    s_tx_mutex = xSemaphoreCreateMutex();
    configASSERT(s_tx_mutex);

    xTaskCreate(rx_task, "serial_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Serial bridge ready on UART0 at %d baud", BAUD_RATE);
}

void serial_bridge_emit(uint8_t node_id, const uint8_t *data, uint8_t len)
{
    if (!s_tx_mutex) return;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    uart_write_bytes(UART_PORT, (const char *)&node_id, 1);
    uart_write_bytes(UART_PORT, (const char *)data, len);
    xSemaphoreGive(s_tx_mutex);
}
