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

typedef struct {
    uint32_t (*get_u32)(void *ctx, const char *key, uint32_t def);
    void     (*set_u32)(void *ctx, const char *key, uint32_t val);
    uint16_t (*get_u16)(void *ctx, const char *key, uint16_t def);
    void     (*set_u16)(void *ctx, const char *key, uint16_t val);
    uint8_t  (*get_u8) (void *ctx, const char *key, uint8_t def);
    void     (*set_u8) (void *ctx, const char *key, uint8_t val);

    /** @brief Copy the stored string (or @p def if unset) into @p out, null-terminated within @p out_size. */
    void     (*get_str)(void *ctx, const char *key, char *out, size_t out_size, const char *def);
    void     (*set_str)(void *ctx, const char *key, const char *val);

    /** @brief Erase everything — backs cfg_reset_defaults(). */
    void     (*clear)(void *ctx);

    void *ctx;
} cfg_backend_t;
