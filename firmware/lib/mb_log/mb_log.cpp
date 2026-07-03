/**
 * @file mb_log.cpp
 * @brief Ring buffer implementation backing mb_log.h.
 *
 * Storage is heap-allocated (malloc/free) rather than a fixed static array
 * so mblog_init() can be called again to resize at runtime without a
 * reboot. s_head + s_count track a classic circular buffer: s_head is the
 * oldest entry's index, and (s_head + s_count) % s_capacity is always the
 * next write slot — see mblog_append() for how those two are kept in sync
 * as the buffer fills and then wraps.
 */
#include "mb_log.h"
#include <string.h>
#include <stdlib.h>

static mb_log_entry_t *s_buf      = 0; /**< Heap-allocated ring buffer, sized by the most recent mblog_init(). */
static size_t           s_capacity = 0; /**< Entries s_buf can hold; the capacity passed to mblog_init(). */
static size_t           s_head     = 0; /**< Index of the oldest entry. */
static size_t           s_count    = 0; /**< Number of valid entries. */
static uint32_t         s_total_appended = 0; /**< Ever-growing; see mblog_total_appended(). */

void mblog_init(size_t capacity)
{
    if (s_buf != 0) {
        free(s_buf);
        s_buf = 0;
    }
    s_capacity = capacity;
    s_head     = 0;
    s_count    = 0;
    s_total_appended = 0;
    if (capacity > 0) {
        s_buf = (mb_log_entry_t *)malloc(sizeof(mb_log_entry_t) * capacity);
    }
}

void mblog_append(const mb_log_entry_t *entry)
{
    if (s_buf == 0 || s_capacity == 0) {
        return;
    }
    size_t write_idx = (s_head + s_count) % s_capacity;
    s_buf[write_idx] = *entry;
    if (s_count < s_capacity) {
        s_count++;
    } else {
        /* Buffer was already full: write_idx == s_head, so the oldest
         * entry was just overwritten — advance head to the next-oldest. */
        s_head = (s_head + 1) % s_capacity;
    }
    s_total_appended++;
}

size_t mblog_get_recent(mb_log_entry_t *out, size_t max_count)
{
    size_t n = (max_count < s_count) ? max_count : s_count;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (s_head + s_count - 1 - i + s_capacity) % s_capacity;
        out[i] = s_buf[idx];
    }
    return n;
}

void mblog_clear(void)
{
    s_head  = 0;
    s_count = 0;
}

size_t mblog_count(void)
{
    return s_count;
}

uint32_t mblog_total_appended(void)
{
    return s_total_appended;
}
