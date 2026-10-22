/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Harry Sun
 *
 * USB backend: TinyUSB composite device with one HID interface
 * (keyboard + mouse + consumer reports) and one CDC-ACM interface so the
 * same physical port keeps a usable serial channel while the keyboard runs.
 * Modeled on reference/esp32-cdc-keyboard-main and the IDF tusb_hid example.
 */
#include <string.h>
#include <stdio.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#if CONFIG_TINYUSB_CDC_ENABLED
#include "tusb_cdc_acm.h"
#endif
#if CONFIG_TINYUSB_CDC_ENABLED && defined(CONFIG_KBD_CONSOLE_ON_USB_CDC)
#include "tinyusb_console.h"
#endif
#include "hid_core.h"
#include "hid_report_map.h"
#include "hid_usb.h"

static const char *TAG = "hid_usb";

#if CONFIG_TINYUSB_CDC_ENABLED
enum {
    ITF_NUM_HID = 0,
    ITF_NUM_CDC, /* control interface; the data interface is CDC+1 */
    ITF_NUM_TOTAL = ITF_NUM_CDC + 2, /* control + data */
};
#else
enum {
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL = 1,
};
#endif

#define EPNUM_HID_IN    0x81
#define EPNUM_CDC_NOTIF 0x82
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x83
#define HID_EP_SIZE     16

static const uint8_t s_report_map[] = { KBD_HID_REPORT_MAP };

#if CONFIG_TINYUSB_CDC_ENABLED
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
#define ITF_COUNT 3
#else
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define ITF_COUNT 1
#endif

static const uint8_t s_configuration_descriptor_const[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 200),
    /* Interface, string idx, boot protocol, report desc len, EP In, size, interval */
    /* HID interface */
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_NONE,
                       sizeof(s_report_map), EPNUM_HID_IN, HID_EP_SIZE, 4),
#if CONFIG_TINYUSB_CDC_ENABLED
    /* CDC function: TUD_CDC_DESCRIPTOR emits its own Interface Association
     * Descriptor plus the control and data interfaces. */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#endif
};

static char s_serial[16];
static const char *s_string_descriptor[] = {
    (char[]){0x09, 0x04},         /* 0: language English (0x0409) */
    "Espressif",                  /* 1: Manufacturer */
    "ESP32 Virtual Keyboard",     /* 2: Product */
    s_serial,                     /* 3: Serial */
    "ESP32 Serial",               /* 4: CDC interface */
    "ESP32 HID",                  /* 5: HID interface */
};

static hid_usb_cfg_t s_cfg;

static void usb_send(const uint8_t *report, uint8_t report_id, size_t len)
{
    tud_hid_n_report(0, report_id, report, len);
}

static bool usb_ready(void)
{
    return tud_mounted() && !tud_suspended();
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_cfg.report_map ? s_cfg.report_map : s_report_map;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    if (report_type == HID_REPORT_TYPE_OUTPUT && report_id == HID_RPT_ID_KEYBOARD &&
        bufsize >= 1 && s_cfg.on_locks) {
        s_cfg.on_locks(buffer[0]);
    }
}

static void usb_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(TAG, "USB attached");
        if (s_cfg.on_mounted) {
            s_cfg.on_mounted(true);
        }
        break;
    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(TAG, "USB detached");
        if (s_cfg.on_mounted) {
            s_cfg.on_mounted(false);
        }
        break;
    default:
        break;
    }
}

esp_err_t hid_usb_start(const hid_usb_cfg_t *cfg)
{
    if (cfg == NULL || cfg->report_map == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_serial, sizeof(s_serial), "KBD%02X%02X%02X", mac[3], mac[4], mac[5]);

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(usb_event_cb);
    tusb_cfg.descriptor.device = NULL; /* esp_tinyusb default: composite 0xEF/0x02/0x01 */
    tusb_cfg.descriptor.full_speed_config = s_configuration_descriptor_const;
    tusb_cfg.descriptor.string = s_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(s_string_descriptor) / sizeof(s_string_descriptor[0]);
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = s_configuration_descriptor_const;
#endif

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    ESP_RETURN_ON_ERROR(err, TAG, "tinyusb_driver_install");

#if CONFIG_TINYUSB_CDC_ENABLED
    tinyusb_config_cdcacm_t acm_cfg = { 0 }; /* no RX callback: host input is dropped */
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
#endif

#if CONFIG_TINYUSB_CDC_ENABLED && defined(CONFIG_KBD_CONSOLE_ON_USB_CDC)
    ESP_ERROR_CHECK(tinyusb_console_init(TINYUSB_CDC_ACM_0));
    ESP_LOGI(TAG, "console redirected to USB CDC");
#endif

    hid_backend_register(HID_TRANSPORT_USB, usb_send, usb_ready);

#if CONFIG_TINYUSB_CDC_ENABLED
    ESP_LOGI(TAG, "USB HID+CDC composite device started");
#else
    ESP_LOGI(TAG, "USB HID device started (CDC compiled out)");
#endif
    return ESP_OK;
}
