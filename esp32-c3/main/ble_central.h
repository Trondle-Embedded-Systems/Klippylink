#pragma once

#include "esp_err.h"
#include "klip_protocol.h"
#include <stdint.h>
#include <stddef.h>

/** Called when a node sends a notification (telemetry packet). */
typedef void (*ble_response_cb_t)(uint16_t conn_id,
                                   const klip_packet_t *pkt,
                                   uint8_t total_len);

/**
 * Initialise BLE central.
 * Starts scanning for KlipLink GATT-server nodes and connects automatically.
 */
esp_err_t ble_central_init(ble_response_cb_t response_cb);

/**
 * Send a raw klip_packet buffer to a specific node by conn_id.
 * Returns ESP_ERR_NOT_FOUND if no node with that conn_id is connected.
 */
esp_err_t ble_central_send_to_node(uint16_t conn_id,
                                    const uint8_t *data, uint8_t total_len);
