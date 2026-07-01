#include "mock_cfg_backend.h"
#include <string.h>

#define MOCK_CFG_MAX_ENTRIES 32
#define MOCK_CFG_KEY_LEN     32
#define MOCK_CFG_STR_LEN     64

typedef enum { MOCK_TYPE_NONE = 0, MOCK_TYPE_U8, MOCK_TYPE_U16, MOCK_TYPE_U32, MOCK_TYPE_STR } mock_type_t;

typedef struct {
    char        key[MOCK_CFG_KEY_LEN];
    mock_type_t type;
    uint32_t    num; /* holds u8/u16/u32 alike, widened */
    char        str[MOCK_CFG_STR_LEN];
} mock_entry_t;

static mock_entry_t s_entries[MOCK_CFG_MAX_ENTRIES];

static mock_entry_t *find(const char *key)
{
    for (int i = 0; i < MOCK_CFG_MAX_ENTRIES; i++) {
        if (s_entries[i].type != MOCK_TYPE_NONE && strcmp(s_entries[i].key, key) == 0) {
            return &s_entries[i];
        }
    }
    return 0;
}

static mock_entry_t *find_or_create(const char *key)
{
    mock_entry_t *e = find(key);
    if (e != 0) {
        return e;
    }
    for (int i = 0; i < MOCK_CFG_MAX_ENTRIES; i++) {
        if (s_entries[i].type == MOCK_TYPE_NONE) {
            strncpy(s_entries[i].key, key, MOCK_CFG_KEY_LEN - 1);
            return &s_entries[i];
        }
    }
    return 0; /* table full — not expected in these tests */
}

static uint32_t mock_get_u32(void * /*ctx*/, const char *key, uint32_t def)
{
    mock_entry_t *e = find(key);
    return (e != 0 && e->type == MOCK_TYPE_U32) ? e->num : def;
}
static void mock_set_u32(void * /*ctx*/, const char *key, uint32_t val)
{
    mock_entry_t *e = find_or_create(key);
    e->type = MOCK_TYPE_U32;
    e->num = val;
}

static uint16_t mock_get_u16(void * /*ctx*/, const char *key, uint16_t def)
{
    mock_entry_t *e = find(key);
    return (e != 0 && e->type == MOCK_TYPE_U16) ? (uint16_t)e->num : def;
}
static void mock_set_u16(void * /*ctx*/, const char *key, uint16_t val)
{
    mock_entry_t *e = find_or_create(key);
    e->type = MOCK_TYPE_U16;
    e->num = val;
}

static uint8_t mock_get_u8(void * /*ctx*/, const char *key, uint8_t def)
{
    mock_entry_t *e = find(key);
    return (e != 0 && e->type == MOCK_TYPE_U8) ? (uint8_t)e->num : def;
}
static void mock_set_u8(void * /*ctx*/, const char *key, uint8_t val)
{
    mock_entry_t *e = find_or_create(key);
    e->type = MOCK_TYPE_U8;
    e->num = val;
}

static void mock_get_str(void * /*ctx*/, const char *key, char *out, size_t out_size, const char *def)
{
    mock_entry_t *e = find(key);
    const char *src = (e != 0 && e->type == MOCK_TYPE_STR) ? e->str : def;
    strncpy(out, src, out_size - 1);
    out[out_size - 1] = '\0';
}
static void mock_set_str(void * /*ctx*/, const char *key, const char *val)
{
    mock_entry_t *e = find_or_create(key);
    e->type = MOCK_TYPE_STR;
    strncpy(e->str, val, MOCK_CFG_STR_LEN - 1);
    e->str[MOCK_CFG_STR_LEN - 1] = '\0';
}

static void mock_clear(void * /*ctx*/)
{
    memset(s_entries, 0, sizeof(s_entries));
}

cfg_backend_t mock_cfg_backend = {
    mock_get_u32, mock_set_u32,
    mock_get_u16, mock_set_u16,
    mock_get_u8,  mock_set_u8,
    mock_get_str, mock_set_str,
    mock_clear,
    0,
};

void mock_cfg_reset(void)
{
    memset(s_entries, 0, sizeof(s_entries));
}
