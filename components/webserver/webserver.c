/*
 * HTTP + WebSocket server. The single-page UI is embedded into the firmware;
 * everything else flows over one WebSocket endpoint /ws as small JSON frames:
 *   up:   kb (key events), ms (mouse), cc (media), cfg (AP credentials),
 *         feat (sticky/toggle), ping
 *   down: st (status), lock (host LEDs), feat echo, pong, cfg ack
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "hid_core.h"
#include "kbd_settings.h"
#include "wifi_ap.h"
#include "kbd_state.h"
#include "status_led.h"
#include "webserver.h"

static const char *TAG = "webserver";

#define MAX_WS_CLIENTS  4
#define WS_RX_BUF_SIZE  512

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server;
static int s_ws_fds[MAX_WS_CLIENTS] = { -1, -1, -1, -1 };

/* A WS frame is written with two socket calls (header, payload) and broadcasts
 * come from the Wi-Fi/BLE/TinyUSB tasks as well as the HTTPD task, so all
 * writes are serialized. */
static SemaphoreHandle_t s_tx_lock;

/* deferred AP apply: let the ack frame leave before we kill the network */
static char s_pending_ssid[KBD_SSID_MAX_LEN + 1];
static char s_pending_pass[KBD_PASS_MAX_LEN + 1];
static volatile bool s_apply_pending;

static void send_ws_text(int fd, const char *json);

static void ap_apply_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000)); /* let the ack drain first */

    ESP_LOGI(TAG, "applying new AP config: ssid='%s'", s_pending_ssid);
    esp_err_t err = wifi_ap_apply(s_pending_ssid, s_pending_pass);
    if (err == ESP_OK) {
        /* Persist only after the new AP is up: a failed update must not leave
         * credentials behind that make the device unreachable after reboot. */
        err = settings_set_ap(s_pending_ssid, s_pending_pass);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "AP applied but saving settings failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "wifi_ap_apply failed: %s; restoring previous AP", esp_err_to_name(err));
        char ssid[KBD_SSID_MAX_LEN + 1], pass[KBD_PASS_MAX_LEN + 1];
        settings_effective_ap(ssid, sizeof(ssid), pass, sizeof(pass));
        wifi_ap_apply(ssid, pass);
    }
    s_apply_pending = false;
    webserver_notify_status();
    vTaskDelete(NULL);
}

static bool ws_fd_add(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) {
            return true;
        }
    }
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] < 0) {
            s_ws_fds[i] = fd;
            return true;
        }
    }
    return false;
}

static void ws_fd_remove(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = -1;
        }
    }
}

static bool ws_fd_has(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) {
            return true;
        }
    }
    return false;
}

static void broadcast(const char *json)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] >= 0) {
            send_ws_text(s_ws_fds[i], json);
        }
    }
}

/* Escape a string for embedding in a JSON string literal (SSID is arbitrary). */
static void json_escape(char *out, size_t out_len, const char *in)
{
    size_t o = 0;
    if (out_len == 0) {
        return;
    }
    for (; *in != '\0' && o + 1 < out_len; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            if (o + 2 >= out_len) {
                break;
            }
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            if (o + 6 >= out_len) {
                break;
            }
            o += (size_t)snprintf(out + o, out_len - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void status_json(char *json, size_t len)
{
    char ssid[KBD_SSID_MAX_LEN + 1];
    char pass[KBD_PASS_MAX_LEN + 1];
    char ssid_json[KBD_SSID_MAX_LEN * 6 + 1];
    kbd_settings_t cfg;
    settings_effective_ap(ssid, sizeof(ssid), pass, sizeof(pass));
    settings_get(&cfg);
    json_escape(ssid_json, sizeof(ssid_json), ssid);
    snprintf(json, len,
             "{\"t\":\"st\",\"ap\":%d,\"ble\":%d,\"usb\":%d,\"ssid\":\"%s\",\"pass\":%d,\"led\":%d}",
             wifi_ap_client_connected() ? 1 : 0,
             hid_transport_connected(HID_TRANSPORT_BLE) ? 1 : 0,
             hid_transport_connected(HID_TRANSPORT_USB) ? 1 : 0,
             ssid_json,
             pass[0] ? 1 : 0,
             cfg.led_brightness);
}

static void locks_json(char *json, size_t len)
{
    uint8_t led = hid_get_lock_state();
    snprintf(json, len,
             "{\"t\":\"lock\",\"num\":%d,\"caps\":%d,\"scroll\":%d}",
             (led & HID_LED_NUM_LOCK) ? 1 : 0,
             (led & HID_LED_CAPS_LOCK) ? 1 : 0,
             (led & HID_LED_SCROLL_LOCK) ? 1 : 0);
}

static void features_json(char *json, size_t len)
{
    kbd_settings_t cfg;
    settings_get(&cfg);
    snprintf(json, len, "{\"t\":\"feat\",\"sticky\":%d,\"toggle\":%d}",
             cfg.sticky_keys ? 1 : 0, cfg.toggle_keys ? 1 : 0);
}

/* Push the full state to one client (used when a socket is first seen and on
 * an explicit "sync" request). */
static void ws_send_state(int fd)
{
    char json[320];
    status_json(json, sizeof(json));
    send_ws_text(fd, json);
    locks_json(json, sizeof(json));
    send_ws_text(fd, json);
    features_json(json, sizeof(json));
    send_ws_text(fd, json);
}

void webserver_notify_status(void)
{
    char json[320];
    status_json(json, sizeof(json));
    broadcast(json);
}

void webserver_push_locks(void)
{
    char json[96];
    locks_json(json, sizeof(json));
    broadcast(json);
}

static void push_features(void)
{
    char json[96];
    features_json(json, sizeof(json));
    broadcast(json);
}

static void send_ws_text(int fd, const char *json)
{
    if (s_server == NULL || s_tx_lock == NULL) {
        return;
    }
    httpd_ws_frame_t pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    esp_err_t err = httpd_ws_send_frame_async(s_server, fd, &pkt);
    xSemaphoreGive(s_tx_lock);
    if (err != ESP_OK) {
        /* Client went away (or the AP restart tore the socket down). A client
         * that vanishes without a CLOSE frame may have left keys held. The
         * softAP allows one client, so releasing here cannot affect another. */
        ESP_LOGD(TAG, "ws send to fd %d failed: %s", fd, esp_err_to_name(err));
        ws_fd_remove(fd);
        kbd_all_up();
    }
}

/* The web UI sends JSON booleans for key down; the documented protocol uses
 * 0/1 numbers. Accept both. */
static bool json_bool(const cJSON *item, bool *out)
{
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint != 0;
        return true;
    }
    return false;
}

static void handle_kb(const cJSON *root)
{
    const cJSON *down = cJSON_GetObjectItem(root, "d");
    const cJSON *keys = cJSON_GetObjectItem(root, "k");
    bool is_down;
    if (!json_bool(down, &is_down) || !cJSON_IsArray(keys)) {
        return;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, keys) {
        if (cJSON_IsNumber(item) && item->valueint >= 0x00 && item->valueint <= 0xE7) {
            kbd_key((uint8_t)item->valueint, is_down);
        }
    }
}

static void handle_ms(const cJSON *root)
{
    const cJSON *b = cJSON_GetObjectItem(root, "b");
    const cJSON *x = cJSON_GetObjectItem(root, "x");
    const cJSON *y = cJSON_GetObjectItem(root, "y");
    const cJSON *w = cJSON_GetObjectItem(root, "w");
    uint8_t rpt[HID_MOUSE_RPT_LEN] = {0};
    if (cJSON_IsNumber(b)) {
        rpt[0] = (uint8_t)(b->valueint & 0x07);
    }
    if (cJSON_IsNumber(x)) {
        rpt[1] = (uint8_t)x->valueint;
    }
    if (cJSON_IsNumber(y)) {
        rpt[2] = (uint8_t)y->valueint;
    }
    if (cJSON_IsNumber(w)) {
        rpt[3] = (uint8_t)w->valueint;
    }
    hid_send_mouse(rpt);
}

static void handle_cc(const cJSON *root)
{
    const cJSON *u = cJSON_GetObjectItem(root, "u");
    const cJSON *d = cJSON_GetObjectItem(root, "d");
    if (!cJSON_IsNumber(u) || u->valueint < 0 || u->valueint > 0x3FF) {
        return;
    }
    uint8_t rpt[HID_CONSUMER_RPT_LEN] = {0};
    bool pressed = true; /* absent "d" means press */
    if (d != NULL) {
        json_bool(d, &pressed);
    }
    if (pressed) {
        rpt[0] = (uint8_t)(u->valueint & 0xFF);
        rpt[1] = (uint8_t)((u->valueint >> 8) & 0x03);
    }
    hid_send_consumer(rpt);
}

static void handle_mod(const cJSON *root)
{
    const cJSON *u = cJSON_GetObjectItem(root, "u");
    if (cJSON_IsNumber(u) && u->valueint >= 0xE0 && u->valueint <= 0xE7) {
        kbd_modifier_click((uint8_t)u->valueint);
    }
}

static void handle_cfg(int fd, const cJSON *root)
{
    const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *pass = cJSON_GetObjectItem(root, "pass");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(pass)) {
        send_ws_text(fd, "{\"t\":\"cfg\",\"ok\":0,\"err\":\"bad request\"}");
        return;
    }
    esp_err_t err = settings_validate_ap(ssid->valuestring, pass->valuestring);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "invalid AP config: %s", esp_err_to_name(err));
        send_ws_text(fd, "{\"t\":\"cfg\",\"ok\":0,\"err\":\"need 1-32 char SSID and password empty or 8-63 chars\"}");
        return;
    }
    if (s_apply_pending) {
        send_ws_text(fd, "{\"t\":\"cfg\",\"ok\":0,\"err\":\"an update is already being applied\"}");
        return;
    }
    strlcpy(s_pending_ssid, ssid->valuestring, sizeof(s_pending_ssid));
    strlcpy(s_pending_pass, pass->valuestring, sizeof(s_pending_pass));
    s_apply_pending = true;
    send_ws_text(fd, "{\"t\":\"cfg\",\"ok\":1,\"msg\":\"reconnecting to the new network...\"}");
    if (xTaskCreate(ap_apply_task, "ap_apply", 4096, NULL, tskIDLE_PRIORITY + 3, NULL) != pdTRUE) {
        s_apply_pending = false;
        send_ws_text(fd, "{\"t\":\"cfg\",\"ok\":0,\"err\":\"out of memory\"}");
    }
}

static void handle_feat(const cJSON *root)
{
    const cJSON *sticky = cJSON_GetObjectItem(root, "sticky");
    const cJSON *toggle = cJSON_GetObjectItem(root, "toggle");
    bool s, t;
    if (!json_bool(sticky, &s) || !json_bool(toggle, &t)) {
        return;
    }
    settings_set_features(s, t);
    kbd_set_features(s, t);
    push_features();
}

static void handle_led(const cJSON *root)
{
    const cJSON *b = cJSON_GetObjectItem(root, "b");
    if (!cJSON_IsNumber(b) || b->valueint < 1 || b->valueint > 100) {
        return;
    }
    settings_set_led_brightness((uint8_t)b->valueint);
    status_led_set_brightness((uint8_t)b->valueint);
    webserver_notify_status(); /* echo the applied value to every client */
}

static void handle_ws_message(int fd, uint8_t *buf, size_t len)
{
    buf[len] = '\0';
    cJSON *root = cJSON_Parse((char *)buf);
    if (root == NULL) {
        ESP_LOGW(TAG, "bad json from fd %d", fd);
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(root, "t");
    if (cJSON_IsString(type)) {
        if (strcmp(type->valuestring, "kb") == 0) {
            handle_kb(root);
        } else if (strcmp(type->valuestring, "ms") == 0) {
            handle_ms(root);
        } else if (strcmp(type->valuestring, "cc") == 0) {
            handle_cc(root);
        } else if (strcmp(type->valuestring, "mod") == 0) {
            handle_mod(root);
        } else if (strcmp(type->valuestring, "cfg") == 0) {
            handle_cfg(fd, root);
        } else if (strcmp(type->valuestring, "feat") == 0) {
            handle_feat(root);
        } else if (strcmp(type->valuestring, "led") == 0) {
            handle_led(root);
        } else if (strcmp(type->valuestring, "sync") == 0) {
            ws_send_state(fd);
        } else if (strcmp(type->valuestring, "ping") == 0) {
            send_ws_text(fd, "{\"t\":\"pong\"}");
        }
    }
    cJSON_Delete(root);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);

    /* IDF's HTTP server replies to the WebSocket upgrade itself and does NOT
     * invoke this handler for the handshake (httpd_uri() returns right after
     * sending the 101 response). A client is therefore first seen here, on
     * its first frame: register it and push the current state. */
    if (!ws_fd_has(fd)) {
        if (!ws_fd_add(fd)) {
            ESP_LOGW(TAG, "too many WS clients, rejecting fd %d", fd);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "ws client fd %d connected", fd);
        ws_send_state(fd);
    }

    static uint8_t buf[WS_RX_BUF_SIZE + 1];
    httpd_ws_frame_t pkt = { 0 };
    esp_err_t err = httpd_ws_recv_frame(req, &pkt, 0);
    if (err != ESP_OK) {
        ws_fd_remove(fd);
        return err;
    }

    if (pkt.type == HTTPD_WS_TYPE_CLOSE) {
        /* Best-effort release; a dead phone never sent its key-ups. */
        kbd_all_up();
        ws_fd_remove(fd);
        ESP_LOGI(TAG, "ws client fd %d gone", fd);
        return ESP_OK;
    }
    if (pkt.type != HTTPD_WS_TYPE_TEXT) {
        /* Control/binary frame: drain any payload so the stream stays aligned. */
        if (pkt.len > 0 && pkt.len <= WS_RX_BUF_SIZE) {
            pkt.payload = buf;
            httpd_ws_recv_frame(req, &pkt, WS_RX_BUF_SIZE);
        }
        return ESP_OK;
    }
    if (pkt.len > WS_RX_BUF_SIZE) {
        /* Do not try to resync a stream we cannot drain. */
        ESP_LOGW(TAG, "ws frame too large (%u bytes), closing fd %d", (unsigned)pkt.len, fd);
        ws_fd_remove(fd);
        return ESP_FAIL;
    }
    if (pkt.len == 0) {
        return ESP_OK;
    }

    pkt.payload = buf;
    err = httpd_ws_recv_frame(req, &pkt, WS_RX_BUF_SIZE);
    if (err != ESP_OK) {
        ws_fd_remove(fd);
        return err;
    }
    handle_ws_message(fd, buf, pkt.len);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

static const httpd_uri_t s_uris[] = {
    { .uri = "/",       .method = HTTP_GET, .handler = index_handler, .is_websocket = false },
    { .uri = "/ws",     .method = HTTP_GET, .handler = ws_handler,    .is_websocket = true,
      .supported_subprotocol = NULL, .handle_ws_control_frames = true },
};

esp_err_t webserver_start(void)
{
    if (s_server) {
        return ESP_OK;
    }
    s_tx_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_tx_lock != NULL, ESP_ERR_NO_MEM, TAG, "ws tx mutex");

    kbd_state_init();
    kbd_settings_t cfg;
    settings_get(&cfg);
    kbd_set_features(cfg.sticky_keys, cfg.toggle_keys);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd start");
    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &s_uris[i]), TAG, "register %s",
                           s_uris[i].uri);
    }
    ESP_LOGI(TAG, "web server on http://192.168.4.1/");
    return ESP_OK;
}
