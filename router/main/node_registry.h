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
/** Add a node; reuses a disconnected slot with matching BDA if available. */
node_t *node_add(const esp_bd_addr_t bda, uint16_t conn_id);
/** Mark node as disconnected (does NOT compact the array — preserves stable indices). */
void    node_remove(uint16_t conn_id);
node_t *node_by_conn_id(uint16_t conn_id);
node_t *node_by_name(const char *name);
/** Number of currently connected nodes. */
int     node_count_connected(void);
/** Total slots used (including disconnected). Always use node_get() with this. */
int     node_count(void);
node_t *node_get(int idx);
/** Returns the stable array index for conn_id, or -1 if not found. */
int     node_id_by_conn_id(uint16_t conn_id);

/** Update node info from a parsed JSON device-info response payload. */
void node_update_info(node_t *node, const char *json);
/** Update a single endstop state. */
void node_set_endstop(node_t *node, uint8_t endstop_idx, bool triggered);
