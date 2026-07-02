/**
 * @file mb_log.h
 * @brief Modbus traffic log ring buffer (LIB-LOG, completeRealisationPlan.md).
 *
 * Pure logic, no hardware dependency — host-testable. Decouples
 * modbus_master_task (producer, once it exists) from the web UI (consumer)
 * so a slow WebSocket client can never stall the bus.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Single source of truth for the ring buffer's capacity — main.cpp's
 * mblog_init() call and web_server_task.cpp's GET /api/v1/log cap (both
 * Arduino-only, can't share a #define declared in main.cpp) both use this
 * instead of each hardcoding their own copy of the number.
 */
#define MB_LOG_CAPACITY 50u

typedef struct {
    uint32_t timestamp_ms;  /**< From the (eventually) NTP-synced clock. */
    bool     is_tx;         /**< true = we sent it, false = response received. */
    uint8_t  raw[256];
    uint8_t  raw_len;
    char     summary[64];   /**< One-line decoded summary, e.g. "FC04 addr31 start0x0000 cnt5 -> OK". */
} mb_log_entry_t;

/**
 * @brief (Re)initialise the ring buffer with the given capacity.
 * Safe to call again to resize; drops any previously logged entries.
 */
void mblog_init(size_t capacity);

/** @brief Append one entry, overwriting the oldest if the buffer is full. */
void mblog_append(const mb_log_entry_t *entry);

/**
 * @brief Copy up to @p max_count entries into @p out, newest first.
 * @return Number of entries actually copied.
 */
size_t mblog_get_recent(mb_log_entry_t *out, size_t max_count);

/** @brief Discard all entries (capacity is unchanged). */
void mblog_clear(void);

/** @brief Number of entries currently held. */
size_t mblog_count(void);
