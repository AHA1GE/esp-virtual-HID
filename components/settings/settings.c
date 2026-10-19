#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "kbd_settings.h"

static const char *TAG = "settings";
static const char *NS = "kbd_cfg";

static kbd_settings_t s_cfg = {
    .ssid = "",
    .pass = "",
    .sticky_keys = false,
    .toggle_keys = false,
};

/* s_cfg is read from several tasks (web server, Wi-Fi/BLE/USB event callbacks),
 * so every access goes through this mutex. */
static SemaphoreHandle_t s_lock;

static void load_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_cfg.ssid);
    nvs_get_str(h, "ssid", s_cfg.ssid, &len);
    len = sizeof(s_cfg.pass);
    nvs_get_str(h, "pass", s_cfg.pass, &len);
    uint8_t v = 0;
    if (nvs_get_u8(h, "sticky", &v) == ESP_OK) {
        s_cfg.sticky_keys = v != 0;
    }
    if (nvs_get_u8(h, "toggle", &v) == ESP_OK) {
        s_cfg.toggle_keys = v != 0;
    }
    nvs_close(h);
}

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    load_from_nvs();
    ESP_LOGI(TAG, "loaded: ssid='%s' sticky=%d toggle=%d", s_cfg.ssid, s_cfg.sticky_keys, s_cfg.toggle_keys);
    return ESP_OK;
}

void settings_get(kbd_settings_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_lock);
}

esp_err_t settings_validate_ap(const char *ssid, const char *pass)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > KBD_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t plen = pass ? strlen(pass) : 0;
    if (plen > 0 && plen < 8) {
        return ESP_ERR_INVALID_ARG; /* WPA2 requires at least 8 characters */
    }
    if (plen > KBD_PASS_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t settings_set_ap(const char *ssid, const char *pass)
{
    esp_err_t err = settings_validate_ap(ssid, pass);
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_cfg.ssid, ssid, sizeof(s_cfg.ssid));
    strlcpy(s_cfg.pass, pass ? pass : "", sizeof(s_cfg.pass));

    nvs_handle_t h;
    err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_str(h, "ssid", s_cfg.ssid);
        nvs_set_str(h, "pass", s_cfg.pass);
        err = nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "AP credentials saved: ssid='%s'", ssid);
    }
    return err;
}

esp_err_t settings_set_features(bool sticky, bool toggle)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_cfg.sticky_keys = sticky;
    s_cfg.toggle_keys = toggle;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_set_u8(h, "sticky", sticky ? 1 : 0);
        nvs_set_u8(h, "toggle", toggle ? 1 : 0);
        err = nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t settings_erase(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_erase_all(h);
        err = nvs_commit(h);
        nvs_close(h);
    }
    memset(&s_cfg, 0, sizeof(s_cfg));
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "settings erased");
    return err;
}

void settings_default_ssid(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_len, "%s%02X%02X%02X", CONFIG_KBD_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);
}

void settings_effective_ap(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_cfg.ssid[0] != '\0') {
        strlcpy(ssid_out, s_cfg.ssid, ssid_len);
        strlcpy(pass_out, s_cfg.pass, pass_len);
    } else {
        settings_default_ssid(ssid_out, ssid_len);
        pass_out[0] = '\0';
    }
    xSemaphoreGive(s_lock);
}
