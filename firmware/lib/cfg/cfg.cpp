/**
 * @file cfg.cpp
 * @brief Configuration persistence (LIB-NVS) — implementation.
 *
 * Every cfg_* call is a one-line forward to the bound backend's matching
 * callback (see cfg_backend.h) — this file owns no storage of its own, only
 * the single s_backend pointer, so all the type-specific logic and
 * truncation behaviour lives in whichever backend is bound. See cfg.h for
 * the documented contract of each function.
 */
#include "cfg.h"

/** @brief Backend bound by cfg_init(); NULL until then. Every cfg_* call below dereferences it without a NULL check — callers must call cfg_init() first. */
static const cfg_backend_t *s_backend = 0;

void cfg_init(const cfg_backend_t *backend)
{
    s_backend = backend;
}

uint32_t cfg_get_u32(const char *key, uint32_t def) { return s_backend->get_u32(s_backend->ctx, key, def); }
void     cfg_set_u32(const char *key, uint32_t val) { s_backend->set_u32(s_backend->ctx, key, val); }

uint16_t cfg_get_u16(const char *key, uint16_t def) { return s_backend->get_u16(s_backend->ctx, key, def); }
void     cfg_set_u16(const char *key, uint16_t val) { s_backend->set_u16(s_backend->ctx, key, val); }

uint8_t cfg_get_u8(const char *key, uint8_t def) { return s_backend->get_u8(s_backend->ctx, key, def); }
void    cfg_set_u8(const char *key, uint8_t val) { s_backend->set_u8(s_backend->ctx, key, val); }

void cfg_get_str(const char *key, char *out, size_t out_size, const char *def)
{
    s_backend->get_str(s_backend->ctx, key, out, out_size, def);
}
void cfg_set_str(const char *key, const char *val) { s_backend->set_str(s_backend->ctx, key, val); }

void cfg_reset_defaults(void) { s_backend->clear(s_backend->ctx); }
