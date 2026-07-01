#ifdef ARDUINO
#include "cfg_backend_preferences.h"
#include <Preferences.h>
#include <string.h>

#define CFG_NAMESPACE "windmeter"

static Preferences s_prefs;

static uint32_t prefs_get_u32(void * /*ctx*/, const char *key, uint32_t def) { return s_prefs.getUInt(key, def); }
static void     prefs_set_u32(void * /*ctx*/, const char *key, uint32_t val) { s_prefs.putUInt(key, val); }

static uint16_t prefs_get_u16(void * /*ctx*/, const char *key, uint16_t def) { return s_prefs.getUShort(key, def); }
static void     prefs_set_u16(void * /*ctx*/, const char *key, uint16_t val) { s_prefs.putUShort(key, val); }

static uint8_t prefs_get_u8(void * /*ctx*/, const char *key, uint8_t def) { return s_prefs.getUChar(key, def); }
static void    prefs_set_u8(void * /*ctx*/, const char *key, uint8_t val) { s_prefs.putUChar(key, val); }

static void prefs_get_str(void * /*ctx*/, const char *key, char *out, size_t out_size, const char *def)
{
    String s = s_prefs.getString(key, def);
    strncpy(out, s.c_str(), out_size - 1);
    out[out_size - 1] = '\0';
}
static void prefs_set_str(void * /*ctx*/, const char *key, const char *val) { s_prefs.putString(key, val); }

static void prefs_clear(void * /*ctx*/) { s_prefs.clear(); }

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
