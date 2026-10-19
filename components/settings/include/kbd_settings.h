#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KBD_SSID_MAX_LEN 32
#define KBD_PASS_MAX_LEN 63 /* WPA2 PSK passphrase: 8..63 characters */

typedef struct {
    char ssid[KBD_SSID_MAX_LEN + 1];   /* empty means "use default ESP-HID-XXXXXX" */
    char pass[KBD_PASS_MAX_LEN + 1];   /* empty means open network */
    bool sticky_keys;
    bool toggle_keys;
    uint8_t led_brightness;            /* percent, 1..100 */
} kbd_settings_t;

/* nvs_flash_init() + load stored settings (fills defaults if absent) */
esp_err_t settings_init(void);

void settings_get(kbd_settings_t *out);

/* Check SSID length and that the password is empty or 8..63 chars. */
esp_err_t settings_validate_ap(const char *ssid, const char *pass);

/* Validate and persist AP credentials. Call only once the AP is running with them. */
esp_err_t settings_set_ap(const char *ssid, const char *pass);

/* Persist sticky/toggle feature switches. */
esp_err_t settings_set_features(bool sticky, bool toggle);

/* Persist the status LED brightness (1..100%). */
esp_err_t settings_set_led_brightness(uint8_t percent);

/* Erase the settings namespace; caller reboots afterwards. */
esp_err_t settings_erase(void);

/* Compute the default AP SSID: CONFIG_KBD_AP_SSID_PREFIX + last 3 MAC bytes. */
void settings_default_ssid(char *out, size_t out_len);

/* SSID/password actually in effect (stored config or the default SSID, open). */
void settings_effective_ap(char *ssid_out, size_t ssid_len, char *pass_out, size_t pass_len);

#ifdef __cplusplus
}
#endif
