/*
 * ESP32 Virtual Keyboard: softAP + web UI driving BLE HID and/or USB HID
 * (composite keyboard + mouse + consumer reports). Backends self-disable on
 * targets without BLE or without USB-OTG.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "soc/soc_caps.h"
#include "driver/gpio.h"

#include "kbd_settings.h"
#include "status_led.h"
#include "hid_core.h"
#include "hid_usb.h"
#include "hid_ble.h"
#include "wifi_ap.h"
#include "webserver.h"

static const char *TAG = "main";

#define FACTORY_RESET_HOLD_MS 5000

static void usb_state_cb(bool mounted)
{
    hid_notify_state(HID_TRANSPORT_USB, mounted);
    webserver_notify_status();
}

static void usb_locks_cb(uint8_t led)
{
    hid_notify_locks(led);
    webserver_push_locks();
}

static void ble_state_cb(bool connected)
{
    hid_notify_state(HID_TRANSPORT_BLE, connected);
    webserver_notify_status();
}

static void ble_locks_cb(uint8_t led)
{
    hid_notify_locks(led);
    webserver_push_locks();
}

static void wifi_client_cb(bool connected, void *ctx)
{
    (void)ctx;
    status_led_set_mode(connected ? LED_MODE_ACTIVE : LED_MODE_WAITING);
    webserver_notify_status();
}

static void factory_reset_task(void *arg)
{
    const gpio_num_t btn = (gpio_num_t)CONFIG_KBD_FACTORY_RESET_GPIO;
    int held_ms = 0;
    while (1) {
        if (gpio_get_level(btn) == 0) {
            held_ms += 100;
            if (held_ms >= FACTORY_RESET_HOLD_MS) {
                ESP_LOGW(TAG, "factory reset requested");
                settings_erase();
                esp_restart();
            }
        } else {
            held_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* A failure that leaves the device unusable: show it on the LED (red, 10 Hz)
 * instead of silently aborting. */
static void hard_error(const char *what, esp_err_t err)
{
    ESP_LOGE(TAG, "HARD ERROR (%s): %s", what, esp_err_to_name(err));
    status_led_set_mode(LED_MODE_ERROR);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    /* Start the LED first so later failures can be signalled in red. */
    esp_err_t err = status_led_start(CONFIG_KBD_LED_GPIO, CONFIG_KBD_LED_ACTIVE_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status LED init failed: %s", esp_err_to_name(err));
    }

    err = settings_init();
    if (err != ESP_OK) {
        hard_error("settings", err);
    }
    kbd_settings_t cfg0;
    settings_get(&cfg0);
    status_led_set_brightness(cfg0.led_brightness);

    err = hid_core_init();
    if (err != ESP_OK) {
        hard_error("hid_core", err);
    }

    size_t map_len = 0;
    const uint8_t *map = hid_get_report_map(&map_len);
    bool hid_ok = false;

#if SOC_BLE_SUPPORTED
    hid_ble_cfg_t ble_cfg = {
        .report_map = map,
        .report_map_len = map_len,
        .device_name = CONFIG_KBD_BLE_DEVICE_NAME,
        .appearance = 0x03C1, /* ESP_HID_APPEARANCE_KEYBOARD */
        .on_connected = ble_state_cb,
        .on_locks = ble_locks_cb,
    };
    err = hid_ble_start(&ble_cfg);
    if (err == ESP_OK) {
        hid_ok = true;
    } else {
        ESP_LOGE(TAG, "BLE HID backend failed: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGI(TAG, "target has no BLE: BLE HID compiled out");
#endif

#if SOC_USB_OTG_SUPPORTED
    hid_usb_cfg_t usb_cfg = {
        .report_map = map,
        .report_map_len = map_len,
        .on_mounted = usb_state_cb,
        .on_locks = usb_locks_cb,
    };
    err = hid_usb_start(&usb_cfg);
    if (err == ESP_OK) {
        hid_ok = true;
    } else {
        ESP_LOGE(TAG, "USB HID backend failed: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGI(TAG, "target has no USB-OTG: USB HID compiled out");
#endif

    char ssid[33], pass[65];
    settings_effective_ap(ssid, sizeof(ssid), pass, sizeof(pass));
    wifi_ap_callbacks_t ap_cbs = { .on_client = wifi_client_cb };
    err = wifi_ap_start(ssid, pass, &ap_cbs);
    if (err != ESP_OK) {
        hard_error("wifi_ap", err);
    }

    err = webserver_start();
    if (err != ESP_OK) {
        hard_error("webserver", err);
    }

    gpio_config_t btn = {
        .pin_bit_mask = BIT64(CONFIG_KBD_FACTORY_RESET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&btn);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "factory reset button unavailable: %s", esp_err_to_name(err));
    } else if (xTaskCreate(factory_reset_task, "factory_reset", 2048, NULL,
                           tskIDLE_PRIORITY + 1, NULL) != pdTRUE) {
        ESP_LOGW(TAG, "factory reset task not created");
    }

    /* AP is up: breathing until a client joins. If no HID transport came up,
     * nothing can reach the host, so signal that as a hard error. */
    status_led_set_mode(hid_ok ? LED_MODE_WAITING : LED_MODE_ERROR);
    if (!hid_ok) {
        ESP_LOGE(TAG, "no HID transport available");
    }
    ESP_LOGI(TAG, "ready: connect to Wi-Fi '%s' and open http://192.168.4.1/", ssid);
}
