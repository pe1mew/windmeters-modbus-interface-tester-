/**
 * @file ntp_manager.h
 * @brief Time sync — the testable decision/validation core (TASK-NTP,
 *        design/completeRealisationPlan.md).
 *
 * Scope note: mb_log_entry_t.timestamp_ms (lib/mb_log) stores millis()
 * (uptime, monotonic, always available) — it does NOT store real epoch
 * time, because it's a uint32_t and real Unix epoch milliseconds already
 * exceed uint32_t range. That was a deliberate choice made when LIB-LOG
 * was built (uptime ticks are cheap to log; wall-clock is only meaningful
 * once synced anyway). ntp_manager_millis_to_epoch() is how a future
 * display layer turns a logged millis() value into a real date, using a
 * recorded (millis, epoch) pair from whenever sync last happened — no
 * change to mb_log/mb_master needed.
 *
 * Actually driving configTime()/settimeofday() is ntp_task.cpp
 * (Arduino-only). This file has no radio or RTC calls, so it's
 * host-testable.
 */
#pragma once

#include <stdint.h>

/**
 * @brief A calendar timestamp for the manual-set-time fallback path (UTC).
 */
typedef struct {
    uint16_t year;   /**< e.g. 2026. */
    uint8_t  month;  /**< 1-12. */
    uint8_t  day;    /**< 1-31, validated against month/leap year. */
    uint8_t  hour;   /**< 0-23. */
    uint8_t  minute; /**< 0-59. */
    uint8_t  second; /**< 0-59. */
} manual_time_t;

/**
 * @brief Should a sync attempt be made right now?
 *
 * v1 scope is "sync once, when STA first comes up" — no periodic resync.
 * @param sta_connected Current wifi_status_t.sta_connected from wifi_manager.
 * @param already_synced Current ntp_manager_is_synced() value.
 * @return true only when connected and not yet synced; false in every other
 *         combination, including "connected but already synced" — by
 *         design there is no periodic resync in v1.
 */
bool ntp_manager_should_sync(bool sta_connected, bool already_synced);

/**
 * @brief Validate a manually-entered date/time (web UI fallback path).
 * @param t Candidate value. Must not be NULL (not checked — caller's
 *          responsibility, same as the rest of this header's pointer args).
 * @return true if every field is in range for its month/leap year.
 */
bool ntp_manager_validate_manual_time(const manual_time_t *t);

/**
 * @brief Record that a sync happened: at local uptime @p millis_at_sync,
 *        the wall-clock time was @p epoch_at_sync (Unix seconds).
 *
 * Only one reference point is kept — a later call overwrites it. Good
 * enough for a millis()-since-boot converter; not intended to correct for
 * clock drift over long uptimes.
 * @param millis_at_sync millis()-since-boot at the moment of sync.
 * @param epoch_at_sync  Wall-clock time at that same moment, Unix seconds (UTC).
 */
void ntp_manager_record_sync(uint32_t millis_at_sync, uint32_t epoch_at_sync);

/**
 * @brief Whether ntp_manager_record_sync() has ever been called.
 * @return true once at least one sync has been recorded; never resets to
 *         false on its own (only ntp_manager_reset() clears it).
 */
bool ntp_manager_is_synced(void);

/**
 * @brief Convert a millis()-since-boot value (e.g. from an mb_log_entry_t)
 *        into Unix epoch seconds, using the recorded sync reference point.
 * @param millis_value millis()-since-boot timestamp to convert.
 * @return Epoch seconds, or 0 if never synced.
 */
uint32_t ntp_manager_millis_to_epoch(uint32_t millis_value);

/** @brief Reset all recorded state — for test isolation, not a product feature. */
void ntp_manager_reset(void);
