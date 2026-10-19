#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "soc/soc_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

#if SOC_BLE_SUPPORTED

#define HID_BLE_NAME_MAX 15 /* fits in the 31-byte adv payload with HID UUID + appearance */

typedef struct {
    const uint8_t *report_map;
    size_t report_map_len;
    const char *device_name;            /* <= HID_BLE_NAME_MAX chars */
    uint16_t appearance;                /* ESP_HID_APPEARANCE_* */
    void (*on_connected)(bool connected);
    void (*on_locks)(uint8_t led_byte); /* keyboard LED output report from host */
} hid_ble_cfg_t;

esp_err_t hid_ble_start(const hid_ble_cfg_t *cfg);

/* BLE GAP bring-up shared with ble_gap.c (NimBLE variant of the esp_hid example) */
esp_err_t kbd_ble_gap_init(void);
esp_err_t kbd_ble_gap_adv_init(uint16_t appearance, const char *device_name);
esp_err_t kbd_ble_gap_adv_start(void);

#else

static inline esp_err_t hid_ble_start(const hid_ble_cfg_t *cfg)
{
    (void)cfg;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* SOC_BLE_SUPPORTED */

#ifdef __cplusplus
}
#endif
