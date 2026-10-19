#include "soc/soc_caps.h"

#if SOC_BLE_SUPPORTED

/*
 * BLE GAP bring-up (NimBLE): controller init, advertisement fields and
 * security configuration. Just Works pairing (no IO capabilities) with
 * bonding so hosts reconnect without re-pairing.
 * Adapted from the IDF esp_hid_device example (examples/bluetooth/esp_hid_device).
 */
#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/ble.h"
#include "host/ble_sm.h"
#include "hid_ble.h"

static const char *TAG = "kbd_ble_gap";

#define GATT_SVR_SVC_HID_UUID 0x1812

static struct ble_hs_adv_fields s_adv_fields;

esp_err_t kbd_ble_gap_init(void)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_mem_release failed: %d", ret);
        return ret;
    }
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
        return ret;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
        return ret;
    }
    ret = esp_nimble_init();
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_init failed: %d", ret);
        return ret;
    }
    return ESP_OK;
}

esp_err_t kbd_ble_gap_adv_init(uint16_t appearance, const char *device_name)
{
    memset(&s_adv_fields, 0, sizeof(s_adv_fields));

    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = appearance;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.tx_pwr_lvl_is_present = 1;
    s_adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    size_t name_len = strlen(device_name);
    if (name_len > HID_BLE_NAME_MAX) {
        name_len = HID_BLE_NAME_MAX;
        ESP_LOGW(TAG, "device name too long for adv payload, truncated to %d chars", HID_BLE_NAME_MAX);
    }
    s_adv_fields.name = (uint8_t *)device_name;
    s_adv_fields.name_len = name_len;
    s_adv_fields.name_is_complete = 1;

    ble_uuid16_t hid_uuid = BLE_UUID16_INIT(GATT_SVR_SVC_HID_UUID);
    static ble_uuid16_t s_uuid16; /* fields point into this */
    s_uuid16 = hid_uuid;
    s_adv_fields.uuids16 = &s_uuid16;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;

    /* Just Works: no IO capabilities -> no MITM, but bonding + SC on */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    return ESP_OK;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGW(TAG, "advertise complete; reason=%d", event->adv_complete.reason);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* Peer lost its keys (or re-pairing requested): drop our old bond and retry */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* Just Works: nothing to inject for BLE_SM_IOACT_NONE */
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGD(TAG, "mtu update; conn=%d mtu=%d", event->mtu.conn_handle, event->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

esp_err_t kbd_ble_gap_adv_start(void)
{
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

    int rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return rc;
    }
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return rc;
    }
    return ESP_OK;
}


#endif /* SOC_BLE_SUPPORTED */
