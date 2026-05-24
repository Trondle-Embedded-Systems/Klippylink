#include "http_server.h"
#include "node_registry.h"
#include "ble_central.h"
#include "wifi_manager.h"
#include "klip_protocol.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

#define TAG          "HTTP"
#define SERVER_PORT  8765
#define MAX_WS_CLIENTS 4

/* Embedded LED config web UI (web_ui.html, added via EMBED_FILES in CMakeLists) */
extern const uint8_t web_ui_html_start[] asm("_binary_web_ui_html_start");
extern const uint8_t web_ui_html_end[]   asm("_binary_web_ui_html_end");

static httpd_handle_t s_server = NULL;
static int s_ws_fds[MAX_WS_CLIENTS];
static int s_ws_count = 0;

/* ── Helper: send JSON response ─────────────────────────────────────────── */
static esp_err_t send_json(httpd_req_t *req, cJSON *root, int status_code)
{
    char *json = cJSON_PrintUnformatted(root);
    if (!json) return ESP_ERR_NO_MEM;
    if (status_code != 200)
        httpd_resp_set_status(req, status_code == 400 ? "400 Bad Request"
                                  : status_code == 404 ? "404 Not Found"
                                  : "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* Extract URI segment after a prefix, e.g. /api/nodes/{X}/... → X */
static bool uri_segment(const char *uri, const char *prefix,
                         char *out, size_t out_len, const char **rest)
{
    if (strncmp(uri, prefix, strlen(prefix)) != 0) return false;
    const char *start = uri + strlen(prefix);
    const char *slash = strchr(start, '/');
    size_t len = slash ? (size_t)(slash - start) : strlen(start);
    if (len == 0 || len >= out_len) return false;
    memcpy(out, start, len);
    out[len] = '\0';
    if (rest) *rest = slash ? slash : start + len;
    return true;
}

/* ── GET /api/info ──────────────────────────────────────────────────────── */
static esp_err_t handle_info(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware", "klippylink-router");
    cJSON_AddStringToObject(root, "ip", wifi_manager_ip());
    cJSON_AddNumberToObject(root, "nodes", node_count());
    esp_err_t ret = send_json(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

/* ── GET /api/nodes ─────────────────────────────────────────────────────── */
static esp_err_t handle_nodes(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < node_count(); i++) {
        node_t *n = node_get(i);
        if (!n) continue;
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", n->name);
        cJSON_AddBoolToObject(obj,   "connected", n->connected);
        cJSON_AddNumberToObject(obj, "neopixels", n->neopixel_count);
        cJSON_AddNumberToObject(obj, "endstops",  n->endstop_count);
        cJSON_AddItemToArray(arr, obj);
    }
    esp_err_t ret = send_json(req, arr, 200);
    cJSON_Delete(arr);
    return ret;
}

/* ── GET /api/nodes/{node}/endstops[/{name}] ────────────────────────────── */
static esp_err_t handle_endstops(httpd_req_t *req)
{
    char node_name[NODE_NAME_LEN];
    const char *rest;
    if (!uri_segment(req->uri, "/api/nodes/", node_name, sizeof(node_name), &rest)) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "bad uri");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }

    node_t *node = node_by_name(node_name);
    if (!node) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "node not found");
        esp_err_t r = send_json(req, e, 404); cJSON_Delete(e); return r;
    }

    /* /endstops/{name} */
    char es_name[NODE_NAME_LEN] = {0};
    const char *endstop_prefix = "/endstops/";
    if (strncmp(rest, endstop_prefix, strlen(endstop_prefix)) == 0) {
        strlcpy(es_name, rest + strlen(endstop_prefix), sizeof(es_name));
        for (int i = 0; i < node->endstop_count; i++) {
            if (strcmp(node->endstops[i].name, es_name) == 0) {
                cJSON *obj = cJSON_CreateObject();
                cJSON_AddStringToObject(obj, "name", es_name);
                cJSON_AddBoolToObject(obj, "triggered", node->endstops[i].triggered);
                esp_err_t r = send_json(req, obj, 200); cJSON_Delete(obj); return r;
            }
        }
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "endstop not found");
        esp_err_t r = send_json(req, e, 404); cJSON_Delete(e); return r;
    }

    /* /endstops — list all */
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < node->endstop_count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name",      node->endstops[i].name);
        cJSON_AddBoolToObject(obj,   "triggered",  node->endstops[i].triggered);
        cJSON_AddItemToArray(arr, obj);
    }
    esp_err_t ret = send_json(req, arr, 200);
    cJSON_Delete(arr);
    return ret;
}

/* ── POST /api/nodes/{node}/neopixels/{strip}/set ───────────────────────── */
static esp_err_t handle_neopixel_set(httpd_req_t *req)
{
    char node_name[NODE_NAME_LEN];
    const char *rest;
    if (!uri_segment(req->uri, "/api/nodes/", node_name, sizeof(node_name), &rest)) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","bad uri");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }

    node_t *node = node_by_name(node_name);
    if (!node) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","node not found");
        esp_err_t r = send_json(req, e, 404); cJSON_Delete(e); return r;
    }

    /* Read body */
    char body[256];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","invalid json");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }

    cJSON *jr = cJSON_GetObjectItem(root, "r");
    cJSON *jg = cJSON_GetObjectItem(root, "g");
    cJSON *jb = cJSON_GetObjectItem(root, "b");
    cJSON *ji = cJSON_GetObjectItem(root, "index");

    uint8_t r = jr ? (uint8_t)jr->valueint : 0;
    uint8_t g = jg ? (uint8_t)jg->valueint : 0;
    uint8_t b = jb ? (uint8_t)jb->valueint : 0;

    uint8_t buf[KLIP_MAX_PACKET_SIZE];
    klip_packet_t *pkt = (klip_packet_t *)buf;
    pkt->magic = KLIPPROTO_MAGIC;

    if (ji && cJSON_IsNumber(ji)) {
        klip_neopixel_set_t *p = (klip_neopixel_set_t *)pkt->payload;
        pkt->command = KLIPCMD_NEOPIXEL_SET;
        pkt->length  = sizeof(*p);
        p->led_idx = (uint8_t)ji->valueint;
        p->r = r; p->g = g; p->b = b;
    } else {
        klip_neopixel_set_all_t *p = (klip_neopixel_set_all_t *)pkt->payload;
        pkt->command = KLIPCMD_NEOPIXEL_SET_ALL;
        pkt->length  = sizeof(*p);
        p->r = r; p->g = g; p->b = b;
    }

    cJSON_Delete(root);
    ble_central_send_to_node(node->conn_id, buf, KLIP_HEADER_SIZE + pkt->length);

    cJSON *ok = cJSON_CreateObject(); cJSON_AddBoolToObject(ok, "ok", true);
    esp_err_t ret = send_json(req, ok, 200); cJSON_Delete(ok);
    return ret;
}

/* ── POST /api/nodes/{node}/leds/zones ──────────────────────────────────── */
static esp_err_t handle_led_zone_set(httpd_req_t *req)
{
    char node_name[NODE_NAME_LEN];
    const char *rest;
    if (!uri_segment(req->uri, "/api/nodes/", node_name, sizeof(node_name), &rest)) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","bad uri");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }
    node_t *node = node_by_name(node_name);
    if (!node) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","node not found");
        esp_err_t r = send_json(req, e, 404); cJSON_Delete(e); return r;
    }

    char body[512];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","invalid json");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }

    cJSON *jzid  = cJSON_GetObjectItem(root, "zone_id");
    cJSON *jstrp = cJSON_GetObjectItem(root, "strip");
    cJSON *jst   = cJSON_GetObjectItem(root, "start");
    cJSON *jcnt  = cJSON_GetObjectItem(root, "count");
    cJSON *jfx   = cJSON_GetObjectItem(root, "effect");
    cJSON *jc    = cJSON_GetObjectItem(root, "color");
    cJSON *jc2   = cJSON_GetObjectItem(root, "color2");
    cJSON *jbri  = cJSON_GetObjectItem(root, "brightness");
    cJSON *jspd  = cJSON_GetObjectItem(root, "speed");

    if (!jzid || !jst || !jcnt || !jfx || !jc || !jc2 || !jbri || !jspd ||
        cJSON_GetArraySize(jc) < 3 || cJSON_GetArraySize(jc2) < 3) {
        cJSON_Delete(root);
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","missing fields");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }

    uint8_t buf[KLIP_HEADER_SIZE + sizeof(klip_led_zone_t)];
    klip_packet_t   *pkt = (klip_packet_t *)buf;
    klip_led_zone_t *z   = (klip_led_zone_t *)pkt->payload;

    pkt->magic   = KLIPPROTO_MAGIC;
    pkt->command = KLIPCMD_LED_ZONE_SET;
    pkt->length  = sizeof(klip_led_zone_t);

    z->zone_id    = (uint8_t)jzid->valueint;
    z->strip_idx  = jstrp ? (uint8_t)jstrp->valueint : 0;
    z->start      = (uint8_t)jst->valueint;
    z->count      = (uint8_t)jcnt->valueint;
    z->effect     = (uint8_t)jfx->valueint;
    z->r          = (uint8_t)cJSON_GetArrayItem(jc,  0)->valueint;
    z->g          = (uint8_t)cJSON_GetArrayItem(jc,  1)->valueint;
    z->b          = (uint8_t)cJSON_GetArrayItem(jc,  2)->valueint;
    z->r2         = (uint8_t)cJSON_GetArrayItem(jc2, 0)->valueint;
    z->g2         = (uint8_t)cJSON_GetArrayItem(jc2, 1)->valueint;
    z->b2         = (uint8_t)cJSON_GetArrayItem(jc2, 2)->valueint;
    z->brightness = (uint8_t)jbri->valueint;
    z->speed      = (uint8_t)jspd->valueint;

    cJSON_Delete(root);
    ble_central_send_to_node(node->conn_id, buf, sizeof(buf));

    cJSON *ok = cJSON_CreateObject(); cJSON_AddBoolToObject(ok, "ok", true);
    esp_err_t ret = send_json(req, ok, 200); cJSON_Delete(ok);
    return ret;
}

/* ── DELETE /api/nodes/{node}/leds/zones/{id} ───────────────────────────── */
static esp_err_t handle_led_zone_clr(httpd_req_t *req)
{
    char node_name[NODE_NAME_LEN];
    const char *rest;
    if (!uri_segment(req->uri, "/api/nodes/", node_name, sizeof(node_name), &rest)) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","bad uri");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }
    node_t *node = node_by_name(node_name);
    if (!node) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","node not found");
        esp_err_t r = send_json(req, e, 404); cJSON_Delete(e); return r;
    }

    /* URI tail: /leds/zones/{id} */
    const char *id_ptr = strrchr(rest, '/');
    uint8_t zone_id = id_ptr ? (uint8_t)atoi(id_ptr + 1) : 0;

    uint8_t buf[KLIP_HEADER_SIZE + 1];
    klip_packet_t *pkt = (klip_packet_t *)buf;
    pkt->magic      = KLIPPROTO_MAGIC;
    pkt->command    = KLIPCMD_LED_ZONE_CLR;
    pkt->length     = 1;
    pkt->payload[0] = zone_id;

    ble_central_send_to_node(node->conn_id, buf, sizeof(buf));

    cJSON *ok = cJSON_CreateObject(); cJSON_AddBoolToObject(ok, "ok", true);
    esp_err_t ret = send_json(req, ok, 200); cJSON_Delete(ok);
    return ret;
}

/* ── Dispatch POST /api/nodes/* ─────────────────────────────────────────── */
static esp_err_t handle_nodes_post(httpd_req_t *req)
{
    if (strstr(req->uri, "/leds/zones"))
        return handle_led_zone_set(req);
    return handle_neopixel_set(req);
}

/* ── GET / (LED config web UI) ──────────────────────────────────────────── */
static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, (const char *)web_ui_html_start,
                    (ssize_t)(web_ui_html_end - web_ui_html_start));
    return ESP_OK;
}

/* ── POST /api/wifi ─────────────────────────────────────────────────────── */
static esp_err_t handle_wifi_config(httpd_req_t *req)
{
    char body[256];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","invalid json");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }

    cJSON *js = cJSON_GetObjectItem(root, "ssid");
    cJSON *jp = cJSON_GetObjectItem(root, "password");
    if (!js || !jp) {
        cJSON_Delete(root);
        cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e,"error","missing ssid/password");
        esp_err_t r = send_json(req, e, 400); cJSON_Delete(e); return r;
    }

    wifi_manager_set_credentials(js->valuestring, jp->valuestring);
    cJSON_Delete(root);

    cJSON *ok = cJSON_CreateObject();
    cJSON_AddBoolToObject(ok, "ok", true);
    cJSON_AddStringToObject(ok, "message", "Credentials saved. Reboot to apply.");
    esp_err_t ret = send_json(req, ok, 200); cJSON_Delete(ok);
    return ret;
}

/* ── WebSocket /ws/events (requires CONFIG_HTTPD_WS_SUPPORT=y) ───────────── */
#ifdef CONFIG_HTTPD_WS_SUPPORT
static esp_err_t handle_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        if (s_ws_count < MAX_WS_CLIENTS) {
            s_ws_fds[s_ws_count++] = httpd_req_to_sockfd(req);
            ESP_LOGI(TAG, "WS client connected (total %d)", s_ws_count);
        }
        return ESP_OK;
    }
    httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT};
    return httpd_ws_recv_frame(req, &frame, 0);
}
#endif /* CONFIG_HTTPD_WS_SUPPORT */

void http_server_broadcast(const char *json)
{
#ifdef CONFIG_HTTPD_WS_SUPPORT
    if (!s_server || s_ws_count == 0) return;
    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len     = strlen(json),
    };
    for (int i = s_ws_count - 1; i >= 0; i--) {
        esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &frame);
        if (ret != ESP_OK) {
            s_ws_count--;
            s_ws_fds[i] = s_ws_fds[s_ws_count];
        }
    }
#else
    (void)json;
#endif
}

/* ── URI table (REST only — WS registered separately below) ─────────────── */
static const httpd_uri_t s_uris[] = {
    {.uri = "/",             .method = HTTP_GET,    .handler = handle_root,         .user_ctx = NULL},
    {.uri = "/api/info",     .method = HTTP_GET,    .handler = handle_info,         .user_ctx = NULL},
    {.uri = "/api/nodes",    .method = HTTP_GET,    .handler = handle_nodes,        .user_ctx = NULL},
    {.uri = "/api/nodes/*",  .method = HTTP_GET,    .handler = handle_endstops,     .user_ctx = NULL},
    {.uri = "/api/nodes/*",  .method = HTTP_POST,   .handler = handle_nodes_post,   .user_ctx = NULL},
    {.uri = "/api/nodes/*",  .method = HTTP_DELETE, .handler = handle_led_zone_clr, .user_ctx = NULL},
    {.uri = "/api/wifi",     .method = HTTP_POST,   .handler = handle_wifi_config,  .user_ctx = NULL},
};

esp_err_t http_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = SERVER_PORT;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 12;
    cfg.max_open_sockets = 8;

    esp_err_t ret = httpd_start(&s_server, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    for (int i = 0; i < (int)(sizeof(s_uris)/sizeof(s_uris[0])); i++)
        httpd_register_uri_handler(s_server, &s_uris[i]);

#ifdef CONFIG_HTTPD_WS_SUPPORT
    static const httpd_uri_t ws_uri = {
        .uri          = "/ws/events",
        .method       = HTTP_GET,
        .handler      = handle_ws,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws_uri);
    ESP_LOGI(TAG, "WebSocket events on ws://…:%d/ws/events", SERVER_PORT);
#endif

    ESP_LOGI(TAG, "HTTP server started on port %d", SERVER_PORT);
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
