#include "node_registry.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

#define TAG "NODE_REG"

static node_t s_nodes[NODE_MAX_COUNT];
static int    s_count = 0;

void node_registry_init(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));
    s_count = 0;
}

node_t *node_add(const esp_bd_addr_t bda, uint16_t conn_id)
{
    if (s_count >= NODE_MAX_COUNT) {
        ESP_LOGE(TAG, "Node table full");
        return NULL;
    }
    node_t *n = &s_nodes[s_count++];
    memset(n, 0, sizeof(*n));
    memcpy(n->bda, bda, sizeof(esp_bd_addr_t));
    n->conn_id   = conn_id;
    n->connected = true;
    snprintf(n->name, NODE_NAME_LEN, "node_%02X%02X",
             bda[4], bda[5]);
    ESP_LOGI(TAG, "Node added: %s conn_id=%d", n->name, conn_id);
    return n;
}

void node_remove(uint16_t conn_id)
{
    for (int i = 0; i < s_count; i++) {
        if (s_nodes[i].conn_id == conn_id) {
            ESP_LOGI(TAG, "Node removed: %s", s_nodes[i].name);
            /* Compact the array */
            int remaining = s_count - i - 1;
            if (remaining > 0)
                memmove(&s_nodes[i], &s_nodes[i + 1],
                        remaining * sizeof(node_t));
            s_count--;
            return;
        }
    }
}

node_t *node_by_conn_id(uint16_t conn_id)
{
    for (int i = 0; i < s_count; i++) {
        if (s_nodes[i].conn_id == conn_id) return &s_nodes[i];
    }
    return NULL;
}

node_t *node_by_name(const char *name)
{
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_nodes[i].name, name) == 0) return &s_nodes[i];
    }
    return NULL;
}

int node_count(void)    { return s_count; }
node_t *node_get(int i) { return (i < s_count) ? &s_nodes[i] : NULL; }

void node_update_info(node_t *node, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "name")) && cJSON_IsString(j))
        strlcpy(node->name, j->valuestring, NODE_NAME_LEN);

    cJSON *neo = cJSON_GetObjectItem(root, "neopixels");
    if (neo && cJSON_IsArray(neo)) {
        int cnt = cJSON_GetArraySize(neo);
        node->neopixel_count = cnt < NODE_MAX_NEOPIXELS ? cnt : NODE_MAX_NEOPIXELS;
        for (int i = 0; i < node->neopixel_count; i++) {
            cJSON *item = cJSON_GetArrayItem(neo, i);
            cJSON *n = cJSON_GetObjectItem(item, "name");
            cJSON *c = cJSON_GetObjectItem(item, "count");
            if (n) strlcpy(node->neopixels[i].name, n->valuestring, NODE_NAME_LEN);
            if (c) node->neopixels[i].count = c->valueint;
        }
    }

    cJSON *es = cJSON_GetObjectItem(root, "endstops");
    if (es && cJSON_IsArray(es)) {
        int cnt = cJSON_GetArraySize(es);
        node->endstop_count = cnt < NODE_MAX_ENDSTOPS ? cnt : NODE_MAX_ENDSTOPS;
        for (int i = 0; i < node->endstop_count; i++) {
            cJSON *item = cJSON_GetArrayItem(es, i);
            cJSON *n = cJSON_GetObjectItem(item, "name");
            if (n) strlcpy(node->endstops[i].name, n->valuestring, NODE_NAME_LEN);
        }
    }

    cJSON_Delete(root);
    node->info_received = true;
    ESP_LOGI(TAG, "Node '%s' info updated: %d neopixels, %d endstops",
             node->name, node->neopixel_count, node->endstop_count);
}

void node_set_endstop(node_t *node, uint8_t endstop_idx, bool triggered)
{
    if (endstop_idx < node->endstop_count)
        node->endstops[endstop_idx].triggered = triggered;
}
