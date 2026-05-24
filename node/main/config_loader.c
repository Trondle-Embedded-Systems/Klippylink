#include "config_loader.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

#define TAG         "CONFIG"
#define CONFIG_PATH "/spiffs/config.json"
#define BUF_SIZE    4096

static bool s_mounted = false;

static esp_err_t mount_spiffs(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,
        .max_files              = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
        s_mounted = true;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    return ret;
}

static void parse_neopixels(cJSON *arr, device_cfg_t *cfg)
{
    int total = cJSON_GetArraySize(arr);
    cfg->neopixel_count = total < CONFIG_MAX_NEOPIXELS ? total : CONFIG_MAX_NEOPIXELS;

    for (int i = 0; i < cfg->neopixel_count; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        neopixel_cfg_t *nc = &cfg->neopixels[i];

        cJSON *j;
        if ((j = cJSON_GetObjectItem(item, "name")) && cJSON_IsString(j))
            strlcpy(nc->name, j->valuestring, CONFIG_MAX_NAME_LEN);
        if ((j = cJSON_GetObjectItem(item, "pin")) && cJSON_IsNumber(j))
            nc->gpio = (uint8_t)j->valueint;
        if ((j = cJSON_GetObjectItem(item, "count")) && cJSON_IsNumber(j))
            nc->count = (uint32_t)j->valueint;
    }
}

static void parse_endstops(cJSON *arr, device_cfg_t *cfg)
{
    int total = cJSON_GetArraySize(arr);
    cfg->endstop_count = total < CONFIG_MAX_ENDSTOPS ? total : CONFIG_MAX_ENDSTOPS;

    for (int i = 0; i < cfg->endstop_count; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        endstop_cfg_t *ec = &cfg->endstops[i];

        cJSON *j;
        if ((j = cJSON_GetObjectItem(item, "name")) && cJSON_IsString(j))
            strlcpy(ec->name, j->valuestring, CONFIG_MAX_NAME_LEN);
        if ((j = cJSON_GetObjectItem(item, "pin")) && cJSON_IsNumber(j))
            ec->gpio = (uint8_t)j->valueint;
        ec->pull_up    = cJSON_IsTrue(cJSON_GetObjectItem(item, "pull_up"));
        ec->active_low = cJSON_IsTrue(cJSON_GetObjectItem(item, "active_low"));
    }
}

esp_err_t config_loader_init(device_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strlcpy(cfg->device_name, "klippylink_node", CONFIG_MAX_NAME_LEN);

    esp_err_t ret = mount_spiffs();
    if (ret != ESP_OK) return ret;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "No config.json — using defaults");
        return ESP_ERR_NOT_FOUND;
    }

    char *buf = malloc(BUF_SIZE);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t len = fread(buf, 1, BUF_SIZE - 1, f);
    fclose(f);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *dev = cJSON_GetObjectItem(root, "device");
    if (dev) {
        cJSON *j = cJSON_GetObjectItem(dev, "name");
        if (j && cJSON_IsString(j))
            strlcpy(cfg->device_name, j->valuestring, CONFIG_MAX_NAME_LEN);
    }

    cJSON *neopixels = cJSON_GetObjectItem(root, "neopixels");
    if (neopixels && cJSON_IsArray(neopixels))
        parse_neopixels(neopixels, cfg);

    cJSON *endstops = cJSON_GetObjectItem(root, "endstops");
    if (endstops && cJSON_IsArray(endstops))
        parse_endstops(endstops, cfg);

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded: device='%s' neopixels=%d endstops=%d",
             cfg->device_name, cfg->neopixel_count, cfg->endstop_count);
    return ESP_OK;
}

esp_err_t config_loader_save(const device_cfg_t *cfg)
{
    esp_err_t ret = mount_spiffs();
    if (ret != ESP_OK) return ret;

    cJSON *root = cJSON_CreateObject();

    cJSON *dev = cJSON_CreateObject();
    cJSON_AddStringToObject(dev, "name", cfg->device_name);
    cJSON_AddItemToObject(root, "device", dev);

    cJSON *neo_arr = cJSON_CreateArray();
    for (int i = 0; i < cfg->neopixel_count; i++) {
        const neopixel_cfg_t *nc = &cfg->neopixels[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name",  nc->name);
        cJSON_AddNumberToObject(item, "pin",   nc->gpio);
        cJSON_AddNumberToObject(item, "count", nc->count);
        cJSON_AddItemToArray(neo_arr, item);
    }
    cJSON_AddItemToObject(root, "neopixels", neo_arr);

    cJSON *es_arr = cJSON_CreateArray();
    for (int i = 0; i < cfg->endstop_count; i++) {
        const endstop_cfg_t *ec = &cfg->endstops[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name",       ec->name);
        cJSON_AddNumberToObject(item, "pin",        ec->gpio);
        cJSON_AddBoolToObject(item,   "pull_up",    ec->pull_up);
        cJSON_AddBoolToObject(item,   "active_low", ec->active_low);
        cJSON_AddItemToArray(es_arr, item);
    }
    cJSON_AddItemToObject(root, "endstops", es_arr);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;

    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) { free(json); return ESP_FAIL; }
    fputs(json, f);
    fclose(f);
    free(json);

    ESP_LOGI(TAG, "Config saved");
    return ESP_OK;
}
