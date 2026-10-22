/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Harry Sun */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "soc/soc_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *report_map;      /* raw HID report descriptor */
    size_t report_map_len;
    void (*on_mounted)(bool mounted);   /* USB host attached/detached */
    void (*on_locks)(uint8_t led_byte); /* keyboard LED output from host */
} hid_usb_cfg_t;

#if SOC_USB_OTG_SUPPORTED

esp_err_t hid_usb_start(const hid_usb_cfg_t *cfg);

#else

static inline esp_err_t hid_usb_start(const hid_usb_cfg_t *cfg)
{
    (void)cfg;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* SOC_USB_OTG_SUPPORTED */

#ifdef __cplusplus
}
#endif
