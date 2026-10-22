/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Harry Sun */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Keyboard report state machine: live-mirror key events from the web UI
 * plus sticky/toggle behavior for the on-screen modifier buttons. */
void kbd_state_init(void);
void kbd_set_features(bool sticky, bool toggle); /* clears latches on change */

/* Any key (incl. modifiers 0xE0..0xE7), used by the text box and hold-mode buttons. */
void kbd_key(uint8_t usage, bool down);

/* Modifier button click in sticky/toggle mode (CTRL/ALT/SHIFT/WIN). */
void kbd_modifier_click(uint8_t usage);

/* Release everything (WS closed). */
void kbd_all_up(void);

#ifdef __cplusplus
}
#endif
