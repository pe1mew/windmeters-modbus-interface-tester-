/**
 * @file cfg.h
 * @brief Configuration persistence (LIB-NVS, completeRealisationPlan.md) — public API.
 *
 * Refines completeRealisationPlan.md's sketch (which used Arduino `String`)
 * to a plain C-string API — `String` doesn't exist in the native test
 * build, and every other library in this project (mb_core, led_status,
 * mb_log) is deliberately Arduino-free at its core for the same reason.
 *
 * See cfg_keys.h for the actual NVS keys and their documented defaults.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "cfg_backend.h"

/** @brief Bind the backend to use. Must be called once before any other cfg_* call. */
void cfg_init(const cfg_backend_t *backend);

uint32_t cfg_get_u32(const char *key, uint32_t def);
void     cfg_set_u32(const char *key, uint32_t val);

uint16_t cfg_get_u16(const char *key, uint16_t def);
void     cfg_set_u16(const char *key, uint16_t val);

uint8_t  cfg_get_u8(const char *key, uint8_t def);
void     cfg_set_u8(const char *key, uint8_t val);

/**
 * @brief Copy the stored string (or @p def if the key was never set) into
 *        @p out, null-terminated within @p out_size.
 */
void cfg_get_str(const char *key, char *out, size_t out_size, const char *def);
void cfg_set_str(const char *key, const char *val);

/** @brief Erase every stored setting. Subsequent gets return their defaults again. */
void cfg_reset_defaults(void);
