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

/**
 * @brief Bind the backend to use. Must be called once before any other cfg_* call.
 * @param backend Storage vtable (must outlive all cfg_* calls) — see cfg_backend.h.
 *                Typically cfg_backend_preferences_init()'s return value on
 *                target hardware, or a mock in native tests.
 */
void cfg_init(const cfg_backend_t *backend);

/**
 * @brief Read a u32 setting.
 * @param key Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param def Value returned if @p key was never set.
 * @return The stored value, or @p def if unset.
 */
uint32_t cfg_get_u32(const char *key, uint32_t def);
/**
 * @brief Write a u32 setting, overwriting any prior value.
 * @param key Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param val Value to store.
 */
void     cfg_set_u32(const char *key, uint32_t val);

/**
 * @brief Read a u16 setting.
 * @param key Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param def Value returned if @p key was never set.
 * @return The stored value, or @p def if unset.
 */
uint16_t cfg_get_u16(const char *key, uint16_t def);
/**
 * @brief Write a u16 setting, overwriting any prior value.
 * @param key Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param val Value to store.
 */
void     cfg_set_u16(const char *key, uint16_t val);

/**
 * @brief Read a u8 setting.
 * @param key Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param def Value returned if @p key was never set.
 * @return The stored value, or @p def if unset.
 */
uint8_t  cfg_get_u8(const char *key, uint8_t def);
/**
 * @brief Write a u8 setting, overwriting any prior value.
 * @param key Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param val Value to store.
 */
void     cfg_set_u8(const char *key, uint8_t val);

/**
 * @brief Copy the stored string (or @p def if the key was never set) into
 *        @p out, null-terminated within @p out_size.
 * @param key      Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param out      Destination buffer, caller-owned.
 * @param out_size Size of @p out in bytes; the copy is truncated (and still
 *                 null-terminated) if the stored/default string doesn't fit.
 * @param def      String copied out if @p key was never set.
 */
void cfg_get_str(const char *key, char *out, size_t out_size, const char *def);
/**
 * @brief Write a string setting, overwriting any prior value.
 * @param key Key string, one of the CFG_KEY_* constants in cfg_keys.h.
 * @param val Null-terminated string to store.
 */
void cfg_set_str(const char *key, const char *val);

/** @brief Erase every stored setting. Subsequent gets return their defaults again. */
void cfg_reset_defaults(void);
