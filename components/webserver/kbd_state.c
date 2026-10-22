/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Harry Sun */

#include <string.h>
#include "hid_core.h"
#include "kbd_state.h"

/*
 * held_keys    : non-modifier usages currently held (live mirror + buttons)
 * held_mods    : modifier bits from live-mirror modifier keys / hold-mode buttons
 * latch_mods   : sticky mode - applies to the next non-modifier press only
 * toggle_mods  : toggle mode - stays until the button is clicked again
 *
 * If both features are enabled, toggle wins over sticky.
 */
#define MOD_BIT(usage) (uint8_t)(1 << ((usage) - 0xE0))
#define IS_MODIFIER(usage) ((usage) >= 0xE0 && (usage) <= 0xE7)

static uint8_t s_held_keys[6];
static uint8_t s_held_keys_n;
static uint8_t s_held_mods;
static uint8_t s_latch_mods;
static uint8_t s_toggle_mods;
static bool s_sticky;
static bool s_toggle;

void kbd_state_init(void)
{
}

static void build_and_send(uint8_t extra_mods);

void kbd_set_features(bool sticky, bool toggle)
{
    uint8_t old_toggle = s_toggle_mods;
    s_sticky = sticky;
    s_toggle = toggle;
    if (!sticky) {
        s_latch_mods = 0;
    }
    /* A toggled modifier must not stay stuck on after toggle mode is turned off. */
    if (!toggle) {
        s_toggle_mods = 0;
    }
    if (s_toggle_mods != old_toggle) {
        build_and_send(0);
    }
}

static void build_and_send(uint8_t extra_mods)
{
    uint8_t rpt[HID_KEYBOARD_RPT_LEN] = {0};
    rpt[0] = s_held_mods | s_toggle_mods | extra_mods;
    for (int i = 0; i < s_held_keys_n; i++) {
        rpt[2 + i] = s_held_keys[i];
    }
    hid_send_keyboard(rpt);
}

void kbd_key(uint8_t usage, bool down)
{
    if (IS_MODIFIER(usage)) {
        uint8_t bit = MOD_BIT(usage);
        if (down) {
            s_held_mods |= bit;
        } else {
            s_held_mods &= ~bit;
        }
        build_and_send(0);
        return;
    }

    if (down) {
        for (int i = 0; i < s_held_keys_n; i++) {
            if (s_held_keys[i] == usage) {
                return; /* browser auto-repeat: host repeats on its own */
            }
        }
        if (s_held_keys_n >= 6) {
            return;
        }
        s_held_keys[s_held_keys_n++] = usage;
        /* sticky latches apply to this press only, then clear */
        build_and_send(s_latch_mods);
        s_latch_mods = 0;
    } else {
        int idx = -1;
        for (int i = 0; i < s_held_keys_n; i++) {
            if (s_held_keys[i] == usage) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            return;
        }
        memmove(&s_held_keys[idx], &s_held_keys[idx + 1], s_held_keys_n - idx - 1);
        s_held_keys_n--;
        build_and_send(0);
    }
}

void kbd_modifier_click(uint8_t usage)
{
    if (!IS_MODIFIER(usage)) {
        return;
    }
    uint8_t bit = MOD_BIT(usage);
    if (s_toggle) {
        s_toggle_mods ^= bit;
    } else if (s_sticky) {
        s_latch_mods ^= bit; /* second click cancels the latch */
    } else {
        return; /* hold mode: buttons use kbd_key(), clicks are ignored */
    }
    build_and_send(0);
}

void kbd_all_up(void)
{
    s_held_keys_n = 0;
    s_held_mods = 0;
    s_latch_mods = 0;
    s_toggle_mods = 0; /* UI state is lost on reload, so don't leave hosts with stuck mods */
    hid_release_all();
}
