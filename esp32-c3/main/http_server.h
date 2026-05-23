#pragma once

#include "esp_err.h"

/**
 * Start the HTTP REST + WebSocket server on port 8765.
 *
 * Klipper-facing endpoints:
 *
 *  GET  /api/info                        router identity
 *  GET  /api/nodes                       list all connected BLE nodes
 *  GET  /api/nodes/{node}/endstops       list endstops on a node
 *  GET  /api/nodes/{node}/endstops/{es}  endstop state {triggered: bool}
 *  POST /api/nodes/{node}/neopixels/{strip}/set
 *       body: {"index":<n>,"r":<0-255>,"g":<0-255>,"b":<0-255>}
 *       body: {"r":<0-255>,"g":<0-255>,"b":<0-255>}   (set all)
 *
 *  POST /api/wifi          body: {"ssid":"...","password":"..."}  (save creds)
 *  POST /api/ota/{node}    raw firmware binary — triggers OTA on that node
 *
 *  WS   /ws/events         JSON event stream (endstop changes, OTA status)
 */
esp_err_t http_server_start(void);
void      http_server_stop(void);

/** Broadcast a JSON event string to all WebSocket clients. */
void http_server_broadcast(const char *json);
