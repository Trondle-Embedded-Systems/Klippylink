#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Initialise the UART0 serial bridge at 115200 baud and start the RX task.
 * Received bytes are fed to command_parser_feed() which dispatches them to BLE.
 */
void serial_bridge_init(void);

/**
 * Write one framed packet to the host:
 *   [node_id u8] [klip_packet bytes (len bytes)]
 * Thread-safe; may be called from any FreeRTOS task.
 */
void serial_bridge_emit(uint8_t node_id, const uint8_t *data, uint8_t len);
