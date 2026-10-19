#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_client)(bool connected, void *ctx);
    void *ctx;
} wifi_ap_callbacks_t;

/* Bring up the network stack and start the softAP. */
esp_err_t wifi_ap_start(const char *ssid, const char *pass, const wifi_ap_callbacks_t *cbs);

/* Restart the AP with new credentials (blocks until it is up again). */
esp_err_t wifi_ap_apply(const char *ssid, const char *pass);

bool wifi_ap_client_connected(void);

#ifdef __cplusplus
}
#endif
