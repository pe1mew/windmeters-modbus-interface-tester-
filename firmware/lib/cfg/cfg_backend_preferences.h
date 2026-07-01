/**
 * @file cfg_backend_preferences.h
 * @brief Real cfg_backend_t backed by Arduino's Preferences (ESP32 NVS) —
 *        target hardware only.
 */
#pragma once

#include "cfg_backend.h"

/**
 * @brief Open the "windmeter" Preferences namespace and return a backend bound to it.
 * @return Pointer to a static cfg_backend_t suitable for cfg_init().
 */
const cfg_backend_t *cfg_backend_preferences_init(void);
