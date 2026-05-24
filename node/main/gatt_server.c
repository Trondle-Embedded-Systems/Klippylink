#include "gatt_server.h"
#include "klip_protocol.h"
#include "neopixel.h"
#include "led_effects.h"
#include "endstop.h"
#include "ota_handler.h"
#include "wifi_node.h"
#include "heater.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_device.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

#define TAG "GATT_SRV"

/* ── GATT attribute table indices ─────────────────────────────────────────── */
enum {
    IDX_SVC,

    IDX_CMD_CHAR,
    IDX_CMD_VAL,

    IDX_TEL_CHAR,
    IDX_TEL_VAL,
    IDX_TEL_CCCD,

    IDX_COUNT,
};

/* 128-bit service UUID: 4B4C0000-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
#define KLIP_UUID128_BASE \
    {0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80, \
     0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00}

static const uint16_t SVC_UUID        = 0x4B4C;
static const uint16_t CHAR_CMD_UUID   = 0x4C01;
static const uint16_t CHAR_TEL_UUID   = 0x4C02;
static const uint16_t CCCD_UUID       = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t PRIMARY_SVC_UUID = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t CHAR_DECL_UUID  = ESP_GATT_UUID_CHAR_DECLARE;

static const uint8_t CHAR_PROP_WRITE  = ESP_GATT_CHAR_PROP_BIT_WRITE
                                       | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t CHAR_PROP_NOTIFY = ESP_GATT_CHAR_PROP_BIT_NOTIFY;

static uint8_t cmd_val[KLIP_MAX_PACKET_SIZE];
static uint8_t tel_val[KLIP_MAX_PACKET_SIZE];
static uint8_t cccd_val[2] = {0, 0};

static esp_gatts_attr_db_t gatt_db[IDX_COUNT] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&PRIMARY_SVC_UUID, ESP_GATT_PERM_READ,
         sizeof(uint16_t), sizeof(SVC_UUID), (uint8_t *)&SVC_UUID}
    },
    [IDX_CMD_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECL_UUID, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&CHAR_PROP_WRITE}
    },
    [IDX_CMD_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&CHAR_CMD_UUID,
         ESP_GATT_PERM_WRITE,
         KLIP_MAX_PACKET_SIZE, 0, cmd_val}
    },
    [IDX_TEL_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&CHAR_DECL_UUID, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&CHAR_PROP_NOTIFY}
    },
    [IDX_TEL_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&CHAR_TEL_UUID,
         ESP_GATT_PERM_READ,
         KLIP_MAX_PACKET_SIZE, 0, tel_val}
    },
    [IDX_TEL_CCCD] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&CCCD_UUID,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(cccd_val), sizeof(cccd_val), cccd_val}
    },
};

static uint16_t s_handles[IDX_COUNT];
static uint16_t s_gatts_if = 0;
static uint16_t s_conn_id  = 0xFFFF;
static bool     s_notify_enabled = false;
static const device_cfg_t *s_cfg = NULL;

/* ── Endstop change ISR → notify central ──────────────────────────────────── */
static void endstop_change_cb(uint8_t idx, bool triggered)
{
    uint8_t buf[KLIP_HEADER_SIZE + 2];
    klip_packet_t *pkt = (klip_packet_t *)buf;
    pkt->magic   = KLIPPROTO_MAGIC;
    pkt->command = KLIPCMD_ENDSTOP_STATE;
    pkt->length  = 2;
    pkt->payload[0] = idx;
    pkt->payload[1] = triggered ? 1 : 0;
    gatt_server_notify(buf, sizeof(buf));
}

/* ── OTA status callback ──────────────────────────────────────────────────── */
static void ota_cb(uint8_t status)
{
    uint8_t buf[KLIP_HEADER_SIZE + 1];
    klip_packet_t *pkt = (klip_packet_t *)buf;
    pkt->magic   = KLIPPROTO_MAGIC;
    pkt->command = KLIPCMD_OTA_STATUS;
    pkt->length  = 1;
    pkt->payload[0] = status;
    gatt_server_notify(buf, sizeof(buf));
}

/* ── Build device-info JSON ───────────────────────────────────────────────── */
static void send_device_info(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", s_cfg->device_name);
    cJSON_AddStringToObject(root, "role", "node");

    cJSON *neo = cJSON_CreateArray();
    for (int i = 0; i < s_cfg->neopixel_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name",  s_cfg->neopixels[i].name);
        cJSON_AddNumberToObject(o, "count", s_cfg->neopixels[i].count);
        cJSON_AddItemToArray(neo, o);
    }
    cJSON_AddItemToObject(root, "neopixels", neo);

    cJSON *es = cJSON_CreateArray();
    for (int i = 0; i < s_cfg->endstop_count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", s_cfg->endstops[i].name);
        cJSON_AddItemToArray(es, o);
    }
    cJSON_AddItemToObject(root, "endstops", es);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return;

    size_t jlen = strlen(json);
    uint8_t buf[KLIP_MAX_PACKET_SIZE];
    klip_packet_t *pkt = (klip_packet_t *)buf;
    pkt->magic   = KLIPPROTO_MAGIC;
    pkt->command = KLIPCMD_DEVICE_INFO_RESP;
    size_t payload_len = jlen < KLIP_MAX_PAYLOAD_SIZE ? jlen : KLIP_MAX_PAYLOAD_SIZE - 1;
    pkt->length  = (uint8_t)payload_len;
    memcpy(pkt->payload, json, payload_len);
    free(json);

    gatt_server_notify(buf, KLIP_HEADER_SIZE + pkt->length);
}

/* ── Dispatch an incoming command packet ─────────────────────────────────── */
static void dispatch_packet(const klip_packet_t *pkt)
{
    if (pkt->magic != KLIPPROTO_MAGIC) return;

    switch ((klip_command_t)pkt->command) {
    case KLIPCMD_PING: {
        uint8_t buf[KLIP_HEADER_SIZE];
        klip_packet_t *r = (klip_packet_t *)buf;
        r->magic = KLIPPROTO_MAGIC; r->command = KLIPCMD_PONG; r->length = 0;
        gatt_server_notify(buf, KLIP_HEADER_SIZE);
        break;
    }
    case KLIPCMD_NEOPIXEL_SET:
        if (pkt->length >= 4) {
            klip_neopixel_set_t *p = (klip_neopixel_set_t *)pkt->payload;
            neopixel_set(0, p->led_idx, p->r, p->g, p->b);
        }
        break;
    case KLIPCMD_NEOPIXEL_SET_ALL:
        if (pkt->length >= 3) {
            klip_neopixel_set_all_t *p = (klip_neopixel_set_all_t *)pkt->payload;
            neopixel_set_all(0, p->r, p->g, p->b);
        }
        break;
    case KLIPCMD_NEOPIXEL_SET_RANGE:
        if (pkt->length >= 5) {
            klip_neopixel_set_range_t *p = (klip_neopixel_set_range_t *)pkt->payload;
            neopixel_set_range(0, p->start, p->count, p->r, p->g, p->b);
        }
        break;
    case KLIPCMD_LED_ZONE_SET:
        if (pkt->length >= sizeof(klip_led_zone_t)) {
            const klip_led_zone_t *z = (const klip_led_zone_t *)pkt->payload;
            led_zone_cfg_t cfg = {
                .strip_idx  = z->strip_idx,
                .start      = z->start,
                .count      = z->count,
                .effect     = (led_effect_id_t)z->effect,
                .r  = z->r,  .g  = z->g,  .b  = z->b,
                .r2 = z->r2, .g2 = z->g2, .b2 = z->b2,
                .brightness = z->brightness,
                .speed      = z->speed,
            };
            led_effects_set_zone(z->zone_id, &cfg);
        }
        break;
    case KLIPCMD_LED_ZONE_CLR:
        if (pkt->length >= 1)
            led_effects_clear_zone(pkt->payload[0]);
        break;
    case KLIPCMD_ENDSTOP_QUERY:
        if (pkt->length >= 1) {
            uint8_t idx = pkt->payload[0];
            bool trig = endstop_read(idx);
            endstop_change_cb(idx, trig);
        }
        break;
    case KLIPCMD_DEVICE_INFO_REQ:
        send_device_info();
        break;
    case KLIPCMD_OTA_BEGIN:
        if (pkt->length >= 8) {
            klip_ota_begin_t *p = (klip_ota_begin_t *)pkt->payload;
            ota_begin(p->size, p->crc32, ota_cb);
        }
        break;
    case KLIPCMD_OTA_DATA:
        ota_write(pkt->payload, pkt->length);
        break;
    case KLIPCMD_OTA_END:
        ota_end();
        break;
    case KLIPCMD_WIFI_ENABLE: {
        /* payload: [ssid_len u8, ssid bytes, pass_len u8, pass bytes] */
        if (pkt->length < 2) break;
        uint8_t ssid_len = pkt->payload[0];
        if (pkt->length < 1 + ssid_len + 1) break;
        uint8_t pass_len = pkt->payload[1 + ssid_len];
        if (pkt->length < 1 + ssid_len + 1 + pass_len) break;

        char ssid[33] = {0};
        char pass[65] = {0};
        memcpy(ssid, &pkt->payload[1], ssid_len < 32 ? ssid_len : 32);
        memcpy(pass, &pkt->payload[2 + ssid_len], pass_len < 64 ? pass_len : 64);

        wifi_node_enable(ssid, pass, NULL);
        break;
    }
    case KLIPCMD_HEARTBEAT: {
        /* Echo heartbeat back to central. */
        uint8_t buf[KLIP_HEADER_SIZE];
        klip_packet_t *r = (klip_packet_t *)buf;
        r->magic = KLIPPROTO_MAGIC; r->command = KLIPCMD_HEARTBEAT; r->length = 0;
        gatt_server_notify(buf, KLIP_HEADER_SIZE);
        break;
    }
#ifdef CONFIG_KLIPNODE_VARIANT_FULL
    case KLIPCMD_HEATER_SET:
        if (pkt->length >= sizeof(klip_heater_cfg_t)) {
            const klip_heater_cfg_t *cfg = (const klip_heater_cfg_t *)pkt->payload;
            heater_set_target(cfg->target_temp);
            heater_set_pid(cfg->kp, cfg->ki, cfg->kd);
        }
        break;
#endif
    default:
        ESP_LOGW(TAG, "Unknown command 0x%02X", pkt->command);
        break;
    }
}

/* Forward declaration so the disconnect handler can reference it */
static esp_ble_adv_params_t s_adv_params;

/* ── GATTS event handler ──────────────────────────────────────────────────── */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        s_gatts_if = gatts_if;
        esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, IDX_COUNT, 0);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Attr table creation failed: 0x%X",
                     param->add_attr_tab.status);
            break;
        }
        memcpy(s_handles, param->add_attr_tab.handles,
               IDX_COUNT * sizeof(uint16_t));
        esp_ble_gatts_start_service(s_handles[IDX_SVC]);
        ESP_LOGI(TAG, "GATT attr table created, service started");
        break;

    case ESP_GATTS_WRITE_EVT: {
        if (param->write.handle == s_handles[IDX_TEL_CCCD]) {
            uint16_t cccd;
            memcpy(&cccd, param->write.value, sizeof(cccd));
            s_notify_enabled = (cccd == 0x0001);
            ESP_LOGI(TAG, "Notifications %s", s_notify_enabled ? "enabled" : "disabled");
        } else if (param->write.handle == s_handles[IDX_CMD_VAL]) {
            if (param->write.len >= KLIP_HEADER_SIZE) {
                dispatch_packet((const klip_packet_t *)param->write.value);
            }
        }
        break;
    }

    case ESP_GATTS_CONNECT_EVT:
        s_conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "Central connected, conn_id=%d", s_conn_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_conn_id = 0xFFFF;
        s_notify_enabled = false;
        ESP_LOGI(TAG, "Central disconnected, restarting advertising");
#ifdef CONFIG_KLIPNODE_VARIANT_FULL
        heater_emergency_off();
#endif
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    default:
        break;
    }
}

/* ── GAP advertising ──────────────────────────────────────────────────────── */
static esp_ble_adv_data_t s_adv_data;
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min      = 0x20,
    .adv_int_max      = 0x40,
    .adv_type         = ADV_TYPE_IND,
    .own_addr_type    = BLE_ADDR_TYPE_PUBLIC,
    .channel_map      = ADV_CHNL_ALL,
    .adv_filter_policy= ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if (param->adv_data_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Adv data set failed: 0x%X", param->adv_data_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "Adv data set OK, starting advertising...");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;
    case ESP_GAP_BLE_START_ADV_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Adv start failed: 0x%X", param->adv_start_cmpl.status);
        } else {
            ESP_LOGI(TAG, "Advertising started successfully");
        }
        break;
    default:
        break;
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */
esp_err_t gatt_server_init(const device_cfg_t *cfg)
{
    s_cfg = cfg;

    nvs_flash_init();

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));

    /* Set device name for advertising */
    esp_ble_gap_set_device_name(cfg->device_name);

    /* Build advertising data */
    uint16_t service_uuid = SVC_UUID;
    memset(&s_adv_data, 0, sizeof(s_adv_data));
    s_adv_data.set_scan_rsp        = false;
    s_adv_data.include_name        = true;
    s_adv_data.include_txpower     = false;
    s_adv_data.flag                = ESP_BLE_ADV_FLAG_GEN_DISC
                                   | ESP_BLE_ADV_FLAG_BREDR_UNSUP_TYP;
    s_adv_data.service_uuid_len    = sizeof(service_uuid);
    s_adv_data.p_service_uuid      = (uint8_t *)&service_uuid;
    esp_err_t adv_ret = esp_ble_gap_config_adv_data(&s_adv_data);
    if (adv_ret != ESP_OK) {
        ESP_LOGE(TAG, "Adv data config failed: %s", esp_err_to_name(adv_ret));
    }

    ESP_LOGI(TAG, "GATT server init complete, advertising as '%s'", cfg->device_name);
    return ESP_OK;
}

esp_err_t gatt_server_notify(const uint8_t *data, uint8_t len)
{
    if (s_conn_id == 0xFFFF || !s_notify_enabled) return ESP_ERR_INVALID_STATE;
    return esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                       s_handles[IDX_TEL_VAL],
                                       len, (uint8_t *)data, false);
}
