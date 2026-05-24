#include <string.h>
#include "esp_log.h"
#include "command_parser.h"
#include "ble_central.h"
#include "node_registry.h"
#include "../../shared/protocol/klip_protocol.h"

#define TAG "CMD_PARSER"

static uint8_t rx_buffer[KLIP_MAX_PACKET_SIZE];
static size_t  rx_offset  = 0;
static uint8_t rx_node_id = 0xFF;
static bool    rx_have_nid = false;

void command_parser_init(void)
{
    rx_offset   = 0;
    rx_have_nid = false;
    ESP_LOGI(TAG, "Command parser initialized");
}

void command_parser_feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        /* First byte of each frame is the node_id. */
        if (!rx_have_nid) {
            rx_node_id  = data[i];
            rx_have_nid = true;
            rx_offset   = 0;
            continue;
        }

        if (rx_offset >= sizeof(rx_buffer)) {
            rx_offset   = 0;
            rx_have_nid = false;
            continue;
        }
        rx_buffer[rx_offset++] = data[i];

        if (rx_offset >= KLIP_HEADER_SIZE) {
            klip_packet_t *pkt = (klip_packet_t *)rx_buffer;
            uint16_t magic;
            memcpy(&magic, &pkt->magic, sizeof(magic));
            if (magic == KLIPPROTO_MAGIC) {
                uint8_t total_len = KLIP_HEADER_SIZE + pkt->length;
                if (rx_offset >= total_len) {
                    ESP_LOGD(TAG, "Packet: node_id=%d cmd=0x%02X len=%d",
                             rx_node_id, pkt->command, pkt->length);

                    if (rx_node_id == 0xFF) {
                        /* Broadcast to all connected nodes. */
                        for (int n = 0; n < node_count(); n++) {
                            node_t *node = node_get(n);
                            if (node && node->connected)
                                ble_central_send_to_node(node->conn_id,
                                                         rx_buffer, total_len);
                        }
                    } else {
                        node_t *node = node_get(rx_node_id);
                        if (node && node->connected)
                            ble_central_send_to_node(node->conn_id,
                                                     rx_buffer, total_len);
                        else
                            ESP_LOGW(TAG, "node_id=%d not connected", rx_node_id);
                    }

                    rx_offset   = 0;
                    rx_have_nid = false;
                }
            } else {
                /* Bad magic — discard this byte and retry. */
                memmove(rx_buffer, rx_buffer + 1, --rx_offset);
            }
        }
    }
}
