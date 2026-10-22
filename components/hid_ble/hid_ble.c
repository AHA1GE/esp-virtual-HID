/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Harry Sun */

#include "soc/soc_caps.h"

#if SOC_BLE_SUPPORTED

/*
 * BLE backend on IDF's esp_hid component (esp_hidd), NimBLE host.
 * Mirrors the init order of examples/bluetooth/esp_hid_device:
 *   gap init -> adv init -> esp_hidd_dev_init() -> ble_store_config_init()
 *   -> nimble_port_freertos_init()
 * Advertisement starts on the ESP_HIDD_START_EVENT and restarts after each
 * disconnect so the keyboard stays pairable.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_hidd.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "hid_core.h"
#include "hid_ble.h"

static const char *TAG = "hid_ble";

static hid_ble_cfg_t s_cfg;
static esp_hidd_dev_t *s_dev;
static esp_hid_raw_report_map_t s_report_maps[1];
static volatile bool s_connected;

void ble_store_config_init(void); /* provided by the NimBLE port (bond storage) */

static void host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run(); /* returns only when nimble_port_stop() is executed */
    nimble_port_freertos_deinit();
}

static void send_report(const uint8_t *report, uint8_t report_id, size_t len)
{
    esp_hidd_dev_input_set(s_dev, 0, report_id, (uint8_t *)report, len);
}

static bool ble_ready(void)
{
    return s_connected;
}

static void hidd_event_cb(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "stack started, advertising");
        kbd_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "host connected");
        s_connected = true;
        if (s_cfg.on_connected) {
            s_cfg.on_connected(true);
        }
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        if (param->output.report_id == HID_RPT_ID_KEYBOARD && param->output.length >= 1 &&
            s_cfg.on_locks) {
            s_cfg.on_locks(param->output.data[0]);
        }
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(TAG, "host disconnected");
        s_connected = false;
        if (s_cfg.on_connected) {
            s_cfg.on_connected(false);
        }
        kbd_ble_gap_adv_start();
        break;
    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, "stack stopped");
        break;
    default:
        break;
    }
}

esp_err_t hid_ble_start(const hid_ble_cfg_t *cfg)
{
    if (cfg == NULL || cfg->report_map == NULL || cfg->device_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    s_report_maps[0].data = s_cfg.report_map;
    s_report_maps[0].len = (uint16_t)s_cfg.report_map_len;
    esp_hid_device_config_t hid_cfg = {
        .vendor_id = 0x303A,
        .product_id = 0x2001,
        .version = 0x0100,
        .device_name = s_cfg.device_name,
        .manufacturer_name = "Espressif",
        .serial_number = "0123456789",
        .report_maps = s_report_maps,
        .report_maps_len = 1,
    };

    ESP_RETURN_ON_ERROR(kbd_ble_gap_init(), TAG, "gap init");

    esp_err_t err = kbd_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, s_cfg.device_name);
    ESP_RETURN_ON_ERROR(err, TAG, "adv init");

    err = esp_hidd_dev_init(&hid_cfg, ESP_HID_TRANSPORT_BLE, hidd_event_cb, &s_dev);
    ESP_RETURN_ON_ERROR(err, TAG, "esp_hidd_dev_init");

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_freertos_init(host_task);

    hid_backend_register(HID_TRANSPORT_BLE, send_report, ble_ready);

    ESP_LOGI(TAG, "BLE HID device '%s' started", s_cfg.device_name);
    return ESP_OK;
}


#endif /* SOC_BLE_SUPPORTED */
