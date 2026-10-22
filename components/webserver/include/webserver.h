/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Harry Sun */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webserver_start(void);

/* Broadcast current status to WS clients; call on any state change. */
void webserver_notify_status(void);

/* Broadcast the host keyboard-LED state; call when host updates locks. */
void webserver_push_locks(void);

#ifdef __cplusplus
}
#endif
