#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "klip_protocol.h"
#include "ble_central.h"
#include "command_parser.h"

#define TAG "C3_MAIN"

#define UART_PORT_NUM  UART_NUM_0
#define UART_BUF_SIZE  512

static uint8_t uart_rx_buf[UART_BUF_SIZE];

static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
}

static void send_to_host(const klip_packet_t *pkt, uint8_t total_len)
{
    uart_write_bytes(UART_PORT_NUM, (const char *)pkt, total_len);
}

static void ble_response_callback(const klip_packet_t *pkt, uint8_t total_len)
{
    ESP_LOGI(TAG, "Forwarding BLE response to host (cmd=0x%02X, len=%d)",
             pkt->command, pkt->length);
    send_to_host(pkt, total_len);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Klippylink ESP32-C3 dongle starting...");
    uart_init();

    ble_central_init(ble_response_callback);
    command_parser_init();

    ESP_LOGI(TAG, "Ready. Waiting for commands from host...");

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, uart_rx_buf, UART_BUF_SIZE, pdMS_TO_TICKS(10));
        if (len > 0) {
            command_parser_feed(uart_rx_buf, len);
        }
        ble_central_poll();
    }
}
