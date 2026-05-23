#include "ble_central.h"
#include "node_registry.h"
#include "klip_protocol.h"
#include "http_server.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_bt_device.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG          "BLE_CENTRAL"
#define PROFILE_NUM  1
#define PROFILE_A_APP_ID 0

/* KlipLink service UUID (16-bit) */
#define KLIP_SVC_UUID        0x4B4C
#define KLIP_CHAR_CMD_UUID   0x4C01
#define KLIP_CHAR_TEL_UUID   0x4C02

/* Per-connection state */
typedef struct {
    uint16_t conn_id;
    uint16_t gattc_if;
    uint16_t cmd_handle;   /* write characteristic */
    uint16_t tel_handle;   /* notify characteristic */
    uint16_t cccd_handle;  /* CCC descriptor for telemetry */
    bool     cmd_found;
    bool     tel_found;
    bool     cccd_found;
    bool     notifications_enabled;
} gattc_conn_t;

#define MAX_CONNS 8
static gattc_conn_t s_conns[MAX_CONNS];
static int          s_conn_count = 0;

static ble_response_cb_t s_response_cb = NULL;

/* ── helpers ─────────────────────────────────────────────────────────────── */
static gattc_conn_t *find_conn(uint16_t conn_id)
{
    for (int i = 0; i < s_conn_count; i++)
        if (s_conns[i].conn_id == conn_id) return &s_conns[i];
    return NULL;
}

static gattc_conn_t *alloc_conn(uint16_t conn_id, uint16_t gattc_if)
{
    if (s_conn_count >= MAX_CONNS) return NULL;
    gattc_conn_t *c = &s_conns[s_conn_count++];
    memset(c, 0, sizeof(*c));
    c->conn_id  = conn_id;
    c->gattc_if = gattc_if;
    return c;
}

static void free_conn(uint16_t conn_id)
{
    for (int i = 0; i < s_conn_count; i++) {
        if (s_conns[i].conn_id == conn_id) {
            int remaining = s_conn_count - i - 1;
            if (remaining > 0)
                memmove(&s_conns[i], &s_conns[i+1],
                        remaining * sizeof(gattc_conn_t));
            s_conn_count--;
            return;
        }
    }
}

/* ── Advertising scan ────────────────────────────────────────────────────── */
static esp_ble_scan_params_t s_scan_params = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x30,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
};

/* ── GATTC event handler ─────────────────────────────────────────────────── */
static void gattc_event_handler(esp_gattc_cb_event_t event,
                                 esp_gatt_if_t gattc_if,
                                 esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        esp_ble_gap_set_scan_params(&s_scan_params);
        ESP_LOGI(TAG, "GATTC registered, starting scan...");
        break;

    case ESP_GATTC_CONNECT_EVT: {
        uint16_t conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "Connected to node, conn_id=%d", conn_id);
        gattc_conn_t *c = alloc_conn(conn_id, gattc_if);
        if (!c) { ESP_LOGE(TAG, "No free conn slot"); break; }

        node_t *node = node_add(param->connect.remote_bda, conn_id);
        if (!node) break;

        esp_ble_gattc_search_service(gattc_if, conn_id, NULL);
        break;
    }

    case ESP_GATTC_SEARCH_RES_EVT: {
        if (param->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 &&
            param->search_res.srvc_id.uuid.uuid.uuid16 == KLIP_SVC_UUID) {
            ESP_LOGI(TAG, "Found KlipLink service on conn_id=%d",
                     param->search_res.conn_id);
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
        uint16_t conn_id = param->search_cmpl.conn_id;
        /* Enumerate characteristics */
        uint16_t count = 0;
        esp_gattc_char_elem_t *chars = NULL;
        esp_gatt_status_t st;

        esp_bt_uuid_t svc_uuid = {.len = ESP_UUID_LEN_16,
                                   .uuid.uuid16 = KLIP_SVC_UUID};
        esp_bt_uuid_t cmd_uuid = {.len = ESP_UUID_LEN_16,
                                   .uuid.uuid16 = KLIP_CHAR_CMD_UUID};
        esp_bt_uuid_t tel_uuid = {.len = ESP_UUID_LEN_16,
                                   .uuid.uuid16 = KLIP_CHAR_TEL_UUID};

        gattc_conn_t *c = find_conn(conn_id);
        if (!c) break;

        /* Find CMD char */
        st = esp_ble_gattc_get_char_by_uuid(gattc_if, conn_id,
                                             0, 0xFFFF, cmd_uuid,
                                             NULL, &count);
        if (st == ESP_GATT_OK && count > 0) {
            chars = malloc(count * sizeof(esp_gattc_char_elem_t));
            esp_ble_gattc_get_char_by_uuid(gattc_if, conn_id,
                                            0, 0xFFFF, cmd_uuid,
                                            chars, &count);
            c->cmd_handle = chars[0].char_handle;
            c->cmd_found  = true;
            free(chars);
        }

        /* Find TEL char */
        count = 0;
        st = esp_ble_gattc_get_char_by_uuid(gattc_if, conn_id,
                                             0, 0xFFFF, tel_uuid,
                                             NULL, &count);
        if (st == ESP_GATT_OK && count > 0) {
            chars = malloc(count * sizeof(esp_gattc_char_elem_t));
            esp_ble_gattc_get_char_by_uuid(gattc_if, conn_id,
                                            0, 0xFFFF, tel_uuid,
                                            chars, &count);
            c->tel_handle = chars[0].char_handle;
            c->tel_found  = true;

            /* Find CCCD descriptor */
            uint16_t desc_count = 0;
            esp_ble_gattc_get_attr_count(gattc_if, conn_id,
                                          ESP_GATT_DB_DESCRIPTOR,
                                          0, 0xFFFF,
                                          c->tel_handle, &desc_count);
            if (desc_count > 0) {
                esp_gattc_descr_elem_t *descs =
                    malloc(desc_count * sizeof(esp_gattc_descr_elem_t));
                esp_bt_uuid_t cccd_uuid = {.len = ESP_UUID_LEN_16,
                                            .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG};
                esp_ble_gattc_get_descr_by_char_handle(gattc_if, conn_id,
                    c->tel_handle, cccd_uuid, descs, &desc_count);
                if (desc_count > 0) {
                    c->cccd_handle = descs[0].handle;
                    c->cccd_found  = true;
                }
                free(descs);
            }
            free(chars);
        }

        /* Enable notifications */
        if (c->cccd_found) {
            uint16_t notify_en = 0x0001;
            esp_ble_gattc_write_char_descr(gattc_if, conn_id,
                c->cccd_handle,
                sizeof(notify_en), (uint8_t *)&notify_en,
                ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
        }

        /* Request device info */
        if (c->cmd_found) {
            uint8_t buf[KLIP_HEADER_SIZE];
            klip_packet_t *pkt = (klip_packet_t *)buf;
            pkt->magic   = KLIPPROTO_MAGIC;
            pkt->command = KLIPCMD_DEVICE_INFO_REQ;
            pkt->length  = 0;
            esp_ble_gattc_write_char(gattc_if, conn_id, c->cmd_handle,
                                      KLIP_HEADER_SIZE, buf,
                                      ESP_GATT_WRITE_TYPE_RSP,
                                      ESP_GATT_AUTH_REQ_NONE);
        }
        break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
        uint16_t conn_id = param->notify.conn_id;
        uint16_t len     = param->notify.value_len;
        const uint8_t *data = param->notify.value;

        if (len < KLIP_HEADER_SIZE) break;
        const klip_packet_t *pkt = (const klip_packet_t *)data;
        if (pkt->magic != KLIPPROTO_MAGIC) break;

        /* Update node registry from known notification types */
        node_t *node = node_by_conn_id(conn_id);
        if (node) {
            switch ((klip_command_t)pkt->command) {
            case KLIPCMD_DEVICE_INFO_RESP:
                if (pkt->length > 0) {
                    char json[KLIP_MAX_PAYLOAD_SIZE + 1];
                    memcpy(json, pkt->payload, pkt->length);
                    json[pkt->length] = '\0';
                    node_update_info(node, json);

                    /* Broadcast event */
                    cJSON *ev = cJSON_CreateObject();
                    cJSON_AddStringToObject(ev, "event", "node_connected");
                    cJSON_AddStringToObject(ev, "node",  node->name);
                    char *ej = cJSON_PrintUnformatted(ev);
                    if (ej) { http_server_broadcast(ej); free(ej); }
                    cJSON_Delete(ev);
                }
                break;
            case KLIPCMD_ENDSTOP_STATE:
                if (pkt->length >= 2) {
                    node_set_endstop(node, pkt->payload[0], pkt->payload[1] != 0);

                    cJSON *ev = cJSON_CreateObject();
                    cJSON_AddStringToObject(ev, "event", "endstop");
                    cJSON_AddStringToObject(ev, "node",  node->name);
                    cJSON_AddNumberToObject(ev, "idx",   pkt->payload[0]);
                    cJSON_AddBoolToObject(ev, "triggered", pkt->payload[1] != 0);
                    char *ej = cJSON_PrintUnformatted(ev);
                    if (ej) { http_server_broadcast(ej); free(ej); }
                    cJSON_Delete(ev);
                }
                break;
            default:
                break;
            }
        }

        if (s_response_cb) s_response_cb(conn_id, pkt, (uint8_t)len);
        break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
        uint16_t conn_id = param->disconnect.conn_id;
        ESP_LOGI(TAG, "Node disconnected, conn_id=%d", conn_id);

        node_t *node = node_by_conn_id(conn_id);
        if (node) {
            cJSON *ev = cJSON_CreateObject();
            cJSON_AddStringToObject(ev, "event", "node_disconnected");
            cJSON_AddStringToObject(ev, "node",  node->name);
            char *ej = cJSON_PrintUnformatted(ev);
            if (ej) { http_server_broadcast(ej); free(ej); }
            cJSON_Delete(ev);
        }
        node_remove(conn_id);
        free_conn(conn_id);

        /* Resume scanning */
        esp_ble_gap_start_scanning(0);
        break;
    }

    default:
        break;
    }
}

/* ── GAP event handler ───────────────────────────────────────────────────── */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        esp_ble_gap_start_scanning(0); /* continuous */
        ESP_LOGI(TAG, "Scanning for KlipLink nodes...");
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            uint8_t adv_len = 0;
            uint8_t *adv = esp_ble_resolve_adv_data(
                param->scan_rst.ble_adv,
                ESP_BLE_AD_TYPE_16SRV_CMPL, &adv_len);
            if (adv && adv_len >= 2) {
                uint16_t uuid;
                memcpy(&uuid, adv, 2);
                if (uuid == KLIP_SVC_UUID) {
                    ESP_LOGI(TAG, "Found KlipLink node, connecting...");
                    esp_ble_gap_stop_scanning();
                    esp_ble_gattc_open(PROFILE_A_APP_ID,
                                       param->scan_rst.bda,
                                       param->scan_rst.ble_addr_type,
                                       true);
                }
            }
        }
        break;

    default:
        break;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */
esp_err_t ble_central_init(ble_response_cb_t response_cb)
{
    s_response_cb = response_cb;

    nvs_flash_init();
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gattc_event_handler));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(PROFILE_A_APP_ID));

    ESP_LOGI(TAG, "BLE central initialised");
    return ESP_OK;
}

esp_err_t ble_central_send_to_node(uint16_t conn_id,
                                    const uint8_t *data, uint8_t total_len)
{
    gattc_conn_t *c = find_conn(conn_id);
    if (!c || !c->cmd_found) return ESP_ERR_NOT_FOUND;

    return esp_ble_gattc_write_char(c->gattc_if, conn_id, c->cmd_handle,
                                     total_len, (uint8_t *)data,
                                     ESP_GATT_WRITE_TYPE_RSP,
                                     ESP_GATT_AUTH_REQ_NONE);
}
