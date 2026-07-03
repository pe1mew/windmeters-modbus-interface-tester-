/**
 * @file cfg_backend_preferences.cpp
 * @brief Real cfg_backend_t backed by Arduino's Preferences (ESP32 NVS) —
 *        implementation.
 *
 * Compiled only under ARDUINO (excluded from the native test build, which
 * links mock_cfg_backend instead) — this is the one place in the project
 * that talks to Preferences directly, so a call site never has to reason
 * about NVS key-length limits or the getString()/String round-trip itself
 * (see cfg_keys.h's file header for the 15-char limit and
 * memory/gotcha-log.md for the incident that limit exists to prevent).
 * Every prefs_* function below discards Preferences::put*()'s return value,
 * same as every other Preferences call site in this project — an oversized
 * key silently fails rather than erroring, which is exactly the failure
 * mode test_cfg's test_all_keys_fit_the_15_char_preferences_limit guards
 * against at the cfg_keys.h level instead.
 */
#ifdef ARDUINO
#include "cfg_backend_preferences.h"
#include <Preferences.h>
#include <string.h>

#define CFG_NAMESPACE "windmeter" /**< Preferences (NVS) namespace this whole project's config lives under. */

/** @brief The single Preferences namespace handle every prefs_* callback below operates on; opened by cfg_backend_preferences_init(). */
static Preferences s_prefs;

/**
 * @brief cfg_backend_t::get_u32 — reads via Preferences::getUInt().
 * @param key NVS key to read.
 * @param def Value to return if @p key isn't set.
 * @return Stored value, or @p def if not set.
 */
static uint32_t prefs_get_u32(void * /*ctx*/, const char *key, uint32_t def) { return s_prefs.getUInt(key, def); }
/**
 * @brief cfg_backend_t::set_u32 — writes via Preferences::putUInt(); return value discarded (see file header).
 * @param key NVS key to write.
 * @param val Value to store.
 */
static void     prefs_set_u32(void * /*ctx*/, const char *key, uint32_t val) { s_prefs.putUInt(key, val); }

/**
 * @brief cfg_backend_t::get_u16 — reads via Preferences::getUShort().
 * @param key NVS key to read.
 * @param def Value to return if @p key isn't set.
 * @return Stored value, or @p def if not set.
 */
static uint16_t prefs_get_u16(void * /*ctx*/, const char *key, uint16_t def) { return s_prefs.getUShort(key, def); }
/**
 * @brief cfg_backend_t::set_u16 — writes via Preferences::putUShort(); return value discarded (see file header).
 * @param key NVS key to write.
 * @param val Value to store.
 */
static void     prefs_set_u16(void * /*ctx*/, const char *key, uint16_t val) { s_prefs.putUShort(key, val); }

/**
 * @brief cfg_backend_t::get_u8 — reads via Preferences::getUChar().
 * @param key NVS key to read.
 * @param def Value to return if @p key isn't set.
 * @return Stored value, or @p def if not set.
 */
static uint8_t prefs_get_u8(void * /*ctx*/, const char *key, uint8_t def) { return s_prefs.getUChar(key, def); }
/**
 * @brief cfg_backend_t::set_u8 — writes via Preferences::putUChar(); return value discarded (see file header).
 * @param key NVS key to write.
 * @param val Value to store.
 */
static void    prefs_set_u8(void * /*ctx*/, const char *key, uint8_t val) { s_prefs.putUChar(key, val); }

/**
 * @brief cfg_backend_t::get_str — reads via Preferences::getString(), then
 *        copies into the caller's fixed buffer.
 *
 * Preferences::getString() returns an Arduino String (heap-allocated), so
 * this both bridges to the plain-C-string cfg_backend_t contract and
 * enforces the truncate-and-null-terminate behaviour cfg_backend.h
 * documents — strncpy() doesn't guarantee null termination on truncation,
 * hence the explicit out[out_size - 1] = '\0' afterwards.
 *
 * @param key      NVS key to read.
 * @param out      Destination buffer.
 * @param out_size Capacity of @p out in bytes, including the null terminator.
 * @param def      Value to use if @p key isn't set.
 */
static void prefs_get_str(void * /*ctx*/, const char *key, char *out, size_t out_size, const char *def)
{
    String s = s_prefs.getString(key, def);
    strncpy(out, s.c_str(), out_size - 1);
    out[out_size - 1] = '\0';
}
/**
 * @brief cfg_backend_t::set_str — writes via Preferences::putString(); return value discarded (see file header).
 * @param key NVS key to write.
 * @param val Null-terminated string to store.
 */
static void prefs_set_str(void * /*ctx*/, const char *key, const char *val) { s_prefs.putString(key, val); }

/** @brief cfg_backend_t::clear — erases the whole "windmeter" namespace via Preferences::clear(). Backs cfg_reset_defaults(). */
static void prefs_clear(void * /*ctx*/) { s_prefs.clear(); }

/** @brief The static backend instance returned by cfg_backend_preferences_init(); @c ctx is unused (0) since all state lives in s_prefs. */
static cfg_backend_t s_prefs_backend = {
    prefs_get_u32, prefs_set_u32,
    prefs_get_u16, prefs_set_u16,
    prefs_get_u8,  prefs_set_u8,
    prefs_get_str, prefs_set_str,
    prefs_clear,
    0,
};

const cfg_backend_t *cfg_backend_preferences_init(void)
{
    s_prefs.begin(CFG_NAMESPACE, false);
    return &s_prefs_backend;
}

#endif /* ARDUINO */
