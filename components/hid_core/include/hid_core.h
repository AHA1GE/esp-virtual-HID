#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* HID report IDs, shared by the USB and BLE backends */
#define HID_RPT_ID_KEYBOARD 1
#define HID_RPT_ID_MOUSE    2
#define HID_RPT_ID_CONSUMER 3

/* Report payload lengths (without the report ID byte) */
#define HID_KEYBOARD_RPT_LEN 8 /* modifier, reserved, key1..key6 */
#define HID_MOUSE_RPT_LEN    4 /* buttons, dx, dy, wheel */
#define HID_CONSUMER_RPT_LEN 2 /* 16-bit usage, little endian */

/* Keyboard LED output byte (from host) */
#define HID_LED_NUM_LOCK    (1 << 0)
#define HID_LED_CAPS_LOCK   (1 << 1)
#define HID_LED_SCROLL_LOCK (1 << 2)

/* Keyboard modifier bits (usage 0xE0..0xE7) */
#define HID_MOD_LCTRL  (1 << 0)
#define HID_MOD_LSHIFT (1 << 1)
#define HID_MOD_LALT   (1 << 2)
#define HID_MOD_LGUI   (1 << 3)
#define HID_MOD_RCTRL  (1 << 4)
#define HID_MOD_RSHIFT (1 << 5)
#define HID_MOD_RALT   (1 << 6)
#define HID_MOD_RGUI   (1 << 7)

typedef enum {
    HID_TRANSPORT_USB = 0,
    HID_TRANSPORT_BLE,
} hid_transport_t;

/* Raw HID report map shared by both backends: keyboard(+LEDs), mouse, consumer */
const uint8_t *hid_get_report_map(size_t *len);

/* Set up fan-out state. Call once before starting any backend. */
esp_err_t hid_core_init(void);

/* Backends call these (via wrappers installed by main) to report state. */
void hid_notify_state(hid_transport_t t, bool connected);
void hid_notify_locks(uint8_t led_byte); /* host -> device (num/caps/scroll) */

bool hid_transport_connected(hid_transport_t t);
uint8_t hid_get_lock_state(void); /* last LED byte received from any host */

/*
 * Backends (hid_usb / hid_ble) are thin adapters wired in main: each registers
 * a send function and a "ready to send" probe, then reports host state back
 * through hid_notify_state()/hid_notify_locks().
 */
typedef void (*hid_send_fn_t)(const uint8_t *report, uint8_t report_id, size_t len);
typedef bool (*hid_ready_fn_t)(void);

void hid_backend_register(hid_transport_t t, hid_send_fn_t send, hid_ready_fn_t ready);

/* Reports are fanned out to every started and connected backend. */
void hid_send_keyboard(const uint8_t report[HID_KEYBOARD_RPT_LEN]);
void hid_send_mouse(const uint8_t report[HID_MOUSE_RPT_LEN]);
void hid_send_consumer(const uint8_t report[HID_CONSUMER_RPT_LEN]);
void hid_release_all(void); /* keyboard keys+modifiers up, mouse buttons up */

#ifdef __cplusplus
}
#endif
