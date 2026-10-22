/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Harry Sun */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hid_core.h"
#include "hid_report_map.h"

static const uint8_t s_report_map[] = { KBD_HID_REPORT_MAP };

static SemaphoreHandle_t s_lock;
static bool s_connected[2];
static uint8_t s_lock_state;

const uint8_t *hid_get_report_map(size_t *len)
{
    if (len) {
        *len = sizeof(s_report_map);
    }
    return s_report_map;
}

esp_err_t hid_core_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

void hid_notify_state(hid_transport_t t, bool connected)
{
    if (t != HID_TRANSPORT_USB && t != HID_TRANSPORT_BLE) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_connected[t] = connected;
    xSemaphoreGive(s_lock);
}

void hid_notify_locks(uint8_t led_byte)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_lock_state = led_byte;
    xSemaphoreGive(s_lock);
}

bool hid_transport_connected(hid_transport_t t)
{
    if (t != HID_TRANSPORT_USB && t != HID_TRANSPORT_BLE) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool c = s_connected[t];
    xSemaphoreGive(s_lock);
    return c;
}

uint8_t hid_get_lock_state(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t st = s_lock_state;
    xSemaphoreGive(s_lock);
    return st;
}

typedef struct {
    hid_send_fn_t send;
    hid_ready_fn_t ready;
    bool inited;
} hid_backend_t;

static hid_backend_t s_backends[2];

void hid_backend_register(hid_transport_t t, hid_send_fn_t send, hid_ready_fn_t ready)
{
    if (t != HID_TRANSPORT_USB && t != HID_TRANSPORT_BLE) {
        return;
    }
    s_backends[t].send = send;
    s_backends[t].ready = ready;
    s_backends[t].inited = true;
}

static void fan_out(uint8_t report_id, const uint8_t *report, size_t len)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < 2; i++) {
        if (s_backends[i].inited && s_connected[i] && s_backends[i].send &&
            (!s_backends[i].ready || s_backends[i].ready())) {
            s_backends[i].send(report, report_id, len);
        }
    }
    xSemaphoreGive(s_lock);
}

void hid_send_keyboard(const uint8_t report[HID_KEYBOARD_RPT_LEN])
{
    fan_out(HID_RPT_ID_KEYBOARD, report, HID_KEYBOARD_RPT_LEN);
}

void hid_send_mouse(const uint8_t report[HID_MOUSE_RPT_LEN])
{
    fan_out(HID_RPT_ID_MOUSE, report, HID_MOUSE_RPT_LEN);
}

void hid_send_consumer(const uint8_t report[HID_CONSUMER_RPT_LEN])
{
    fan_out(HID_RPT_ID_CONSUMER, report, HID_CONSUMER_RPT_LEN);
}

void hid_release_all(void)
{
    static const uint8_t zero_kb[HID_KEYBOARD_RPT_LEN] = {0};
    static const uint8_t zero_ms[HID_MOUSE_RPT_LEN] = {0};
    static const uint8_t zero_cc[HID_CONSUMER_RPT_LEN] = {0};
    fan_out(HID_RPT_ID_KEYBOARD, zero_kb, sizeof(zero_kb));
    fan_out(HID_RPT_ID_MOUSE, zero_ms, sizeof(zero_ms));
    fan_out(HID_RPT_ID_CONSUMER, zero_cc, sizeof(zero_cc));
}
