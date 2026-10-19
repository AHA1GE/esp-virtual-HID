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

void app_main(void)
{
    ESP_ERROR_CHECK(settings_init());

    /* LED solid while bring-up runs */
    ESP_ERROR_CHECK(status_led_start(CONFIG_KBD_LED_GPIO, CONFIG_KBD_LED_ACTIVE_LOW));

    ESP_ERROR_CHECK(hid_core_init());
    size_t map_len = 0;
    const uint8_t *map = hid_get_report_map(&map_len);
    esp_err_t err = ESP_OK;

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
    if (err != ESP_OK) {
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
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB HID backend failed: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGI(TAG, "target has no USB-OTG: USB HID compiled out");
#endif

    char ssid[33], pass[65];
    settings_effective_ap(ssid, sizeof(ssid), pass, sizeof(pass));
    wifi_ap_callbacks_t ap_cbs = { .on_client = wifi_client_cb };
    ESP_ERROR_CHECK(wifi_ap_start(ssid, pass, &ap_cbs));

    ESP_ERROR_CHECK(webserver_start());

    gpio_config_t btn = {
        .pin_bit_mask = BIT64(CONFIG_KBD_FACTORY_RESET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));
    xTaskCreate(factory_reset_task, "factory_reset", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    /* AP is up: breathing until a client joins */
    status_led_set_mode(LED_MODE_WAITING);
    ESP_LOGI(TAG, "ready: connect to Wi-Fi '%s' and open http://192.168.4.1/", ssid);
}
