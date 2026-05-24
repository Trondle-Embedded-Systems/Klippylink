#pragma once

#include <stdint.h>

typedef void (*wifi_node_ip_cb_t)(uint32_t ip_addr);

/** Start WiFi STA and connect to ssid/password. Calls cb with the assigned IP. */
void wifi_node_enable(const char *ssid, const char *pass, wifi_node_ip_cb_t cb);

/** Disconnect and stop the WiFi stack. */
void wifi_node_disable(void);
