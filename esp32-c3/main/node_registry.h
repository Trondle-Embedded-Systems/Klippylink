#pragma once

#include "esp_err.h"
#include "esp_bt_defs.h"
#include <stdint.h>
#include <stdbool.h>

#define NODE_MAX_COUNT       8
#define NODE_NAME_LEN        32
#define NODE_MAX_NEOPIXELS   4
#define NODE_MAX_ENDSTOPS    8

typedef struct {
    char name[NODE_NAME_LEN];
    uint32_t count;
} node_neopixel_t;

typedef struct {
    char name[NODE_NAME_LEN];
    bool triggered;
} node_endstop_t;

typedef struct {
    char             name[NODE_NAME_LEN];
    esp_bd_addr_t    bda;
    uint16_t         conn_id;
    bool             connected;
    bool             info_received;

    node_neopixel_t  neopixels[NODE_MAX_NEOPIXELS];
    int              neopixel_count;

    node_endstop_t   endstops[NODE_MAX_ENDSTOPS];
    int              endstop_count;
} node_t;

void    node_registry_init(void);
node_t *node_add(const esp_bd_addr_t bda, uint16_t conn_id);
void    node_remove(uint16_t conn_id);
node_t *node_by_conn_id(uint16_t conn_id);
node_t *node_by_name(const char *name);
int     node_count(void);
node_t *node_get(int idx);

/** Update node info from a parsed JSON device-info response payload. */
void node_update_info(node_t *node, const char *json);
/** Update a single endstop state. */
void node_set_endstop(node_t *node, uint8_t endstop_idx, bool triggered);
