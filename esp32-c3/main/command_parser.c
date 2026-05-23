#include <string.h>
#include "esp_log.h"
#include "command_parser.h"
#include "ble_central.h"
#include "../../shared/protocol/klip_protocol.h"

#define TAG "CMD_PARSER"

static uint8_t rx_buffer[KLIP_MAX_PACKET_SIZE];
static size_t rx_offset = 0;

void command_parser_init(void)
{
    rx_offset = 0;
    ESP_LOGI(TAG, "Command parser initialized");
}

void command_parser_feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (rx_offset >= sizeof(rx_buffer)) {
            rx_offset = 0;
        }
        rx_buffer[rx_offset++] = data[i];

        if (rx_offset >= KLIP_HEADER_SIZE) {
            klip_packet_t *pkt = (klip_packet_t *)rx_buffer;
            uint16_t magic;
            memcpy(&magic, &pkt->magic, sizeof(magic));
            if (magic == KLIPPROTO_MAGIC) {
                uint8_t total_len = KLIP_HEADER_SIZE + pkt->length;
                if (rx_offset >= total_len) {
                    ESP_LOGI(TAG, "Complete packet: cmd=0x%02X len=%d",
                             pkt->command, pkt->length);
                    ble_central_send(pkt, total_len);
                    rx_offset = 0;
                }
            } else {
                memmove(rx_buffer, rx_buffer + 1, --rx_offset);
            }
        }
    }
}
