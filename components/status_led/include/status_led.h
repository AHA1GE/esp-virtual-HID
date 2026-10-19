#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_MODE_BOOT = 0,   /* solid on: starting up */
    LED_MODE_WAITING,    /* breathing: softAP up, no client */
    LED_MODE_ACTIVE,     /* blinking ~2 Hz: client connected */
    LED_MODE_ERROR,      /* red flashing 10 Hz: hard error */
} led_mode_t;

/* Starts the LED service task. Safe to call once. */
esp_err_t status_led_start(int gpio, bool active_low);

void status_led_set_mode(led_mode_t mode);

#ifdef __cplusplus
}
#endif
