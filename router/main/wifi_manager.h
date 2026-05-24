#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialise and connect to WiFi using credentials stored in NVS.
 * Blocks until connected or times out (~10 s).
 * Returns ESP_OK on connection, ESP_ERR_TIMEOUT otherwise.
 */
esp_err_t wifi_manager_init(void);

/** Store SSID + password in NVS so they survive reboot. */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/** Return true when an IP address has been assigned. */
bool wifi_manager_is_connected(void);

/** Return the assigned IP address string, or "0.0.0.0". */
const char *wifi_manager_ip(void);
