/**
 * @file cfg_backend.h
 * @brief Injectable storage backend used by cfg.cpp.
 *
 * Same reason mb_core.cpp never calls Serial2 directly and led_status.cpp
 * never calls FastLED directly: cfg.cpp never calls Arduino's Preferences
 * directly, so it's host-testable against an in-memory mock.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Storage vtable injected into cfg.cpp via cfg_init().
 *
 * Every member is a callback cfg.cpp invokes directly (no vtable dispatch
 * beyond the function pointer itself) with @c ctx as the first argument,
 * unmodified, on every call — implementations that need state (a namespace
 * handle, an in-memory table) stash it behind @c ctx rather than using
 * globals, so a test build can swap backends without process-level state
 * leaking between test cases. Two concrete implementations exist:
 * cfg_backend_preferences.cpp (target hardware, Arduino Preferences/NVS)
 * and test_cfg's mock_cfg_backend.cpp (native unit tests, in-memory map).
 */
typedef struct {
    /** @brief Return the u32 stored at @p key, or @p def if never set. Called by cfg_get_u32(). */
    uint32_t (*get_u32)(void *ctx, const char *key, uint32_t def);
    /** @brief Store @p val at @p key, overwriting any prior value. Called by cfg_set_u32(). */
    void     (*set_u32)(void *ctx, const char *key, uint32_t val);
    /** @brief Return the u16 stored at @p key, or @p def if never set. Called by cfg_get_u16(). */
    uint16_t (*get_u16)(void *ctx, const char *key, uint16_t def);
    /** @brief Store @p val at @p key, overwriting any prior value. Called by cfg_set_u16(). */
    void     (*set_u16)(void *ctx, const char *key, uint16_t val);
    /** @brief Return the u8 stored at @p key, or @p def if never set. Called by cfg_get_u8(). */
    uint8_t  (*get_u8) (void *ctx, const char *key, uint8_t def);
    /** @brief Store @p val at @p key, overwriting any prior value. Called by cfg_set_u8(). */
    void     (*set_u8) (void *ctx, const char *key, uint8_t val);

    /** @brief Copy the stored string (or @p def if unset) into @p out, null-terminated within @p out_size. */
    void     (*get_str)(void *ctx, const char *key, char *out, size_t out_size, const char *def);
    /** @brief Store @p val at @p key, overwriting any prior value. Called by cfg_set_str(). */
    void     (*set_str)(void *ctx, const char *key, const char *val);

    /** @brief Erase everything — backs cfg_reset_defaults(). */
    void     (*clear)(void *ctx);

    /** @brief Opaque state passed unmodified as the first argument to every callback above. May be NULL if the backend needs no state. */
    void *ctx;
} cfg_backend_t;
