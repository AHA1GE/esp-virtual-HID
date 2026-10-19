#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "wifi_ap.h"

static const char *TAG = "wifi_ap";

static wifi_ap_callbacks_t s_cbs;
static volatile bool s_client_connected;
static char s_cur_ssid[33];
static char s_cur_pass[65];

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_EVENT) {
        return;
    }
    switch (id) {
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "softAP '%s' started", s_cur_ssid);
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station joined");
        s_client_connected = true;
        if (s_cbs.on_client) {
            s_cbs.on_client(true, s_cbs.ctx);
        }
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station left");
        s_client_connected = false;
        if (s_cbs.on_client) {
            s_cbs.on_client(false, s_cbs.ctx);
        }
        break;
    case WIFI_EVENT_AP_STOP:
        ESP_LOGI(TAG, "softAP stopped");
        if (s_client_connected) {
            s_client_connected = false;
            if (s_cbs.on_client) {
                s_cbs.on_client(false, s_cbs.ctx);
            }
        }
        break;
    case WIFI_EVENT_AP_PROBEREQRECVED:
        break;
    default:
        break;
    }
}

/* Validate before touching the running AP, so a rejected config never
 * takes the network down. */
static esp_err_t check_ap_args(const char *ssid, const char *pass)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t slen = strlen(ssid);
    /* SSID is not NUL-terminated in wifi_config_t: a 32-char name fills the field. */
    if (slen == 0 || slen > sizeof(((wifi_config_t *)0)->ap.ssid)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t plen = pass ? strlen(pass) : 0;
    if (plen != 0 && (plen < 8 || plen >= sizeof(((wifi_config_t *)0)->ap.password))) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t start_ap(const char *ssid, const char *pass)
{
    esp_err_t err = check_ap_args(ssid, pass);
    if (err != ESP_OK) {
        return err;
    }
    size_t slen = strlen(ssid);
    size_t plen = pass ? strlen(pass) : 0;

    wifi_config_t cfg = { 0 };
    memcpy(cfg.ap.ssid, ssid, slen);
    cfg.ap.ssid_len = (uint8_t)slen;
    if (plen >= 8) {
        memcpy(cfg.ap.password, pass, plen);
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    cfg.ap.max_connection = 1;
    cfg.ap.beacon_interval = 100;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &cfg), TAG, "set_config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    strlcpy(s_cur_ssid, ssid, sizeof(s_cur_ssid));
    strlcpy(s_cur_pass, pass ? pass : "", sizeof(s_cur_pass));
    return ESP_OK;
}

esp_err_t wifi_ap_start(const char *ssid, const char *pass, const wifi_ap_callbacks_t *cbs)
{
    if (cbs) {
        s_cbs = *cbs;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    /* Credentials live in our own NVS namespace; do not keep a second copy in
     * the driver's nvs.net80211 namespace. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_event_handler, NULL), TAG, "handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set mode");

    return start_ap(ssid, pass);
}

esp_err_t wifi_ap_apply(const char *ssid, const char *pass)
{
    esp_err_t err = check_ap_args(ssid, pass);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_stop();
    /* Not started is fine during a restart; only a real stop failure aborts. */
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        return err;
    }
    return start_ap(ssid, pass);
}

bool wifi_ap_client_connected(void)
{
    return s_client_connected;
}
