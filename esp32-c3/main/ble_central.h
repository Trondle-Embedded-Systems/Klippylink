#pragma once

#include <stdint.h>
#include "klip_protocol.h"

typedef void (*ble_response_cb_t)(const klip_packet_t *pkt, uint8_t total_len);

void ble_central_init(ble_response_cb_t response_cb);
void ble_central_send(const klip_packet_t *pkt, uint8_t total_len);
void ble_central_poll(void);
