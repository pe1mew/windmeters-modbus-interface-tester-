/**
 * @file ntp_manager.cpp
 * @brief Time sync decision/validation core — implementation (TASK-NTP,
 *        design/completeRealisationPlan.md).
 *
 * See ntp_manager.h for the design rationale (why mb_log stores millis()
 * instead of epoch time, and how this file bridges the two). The public
 * functions here are pure (no configTime()/settimeofday()/RTC calls) by
 * construction — that's what makes them host-testable in
 * test/test_ntp_manager; their doc comments live at the declaration in
 * ntp_manager.h, not duplicated here.
 */
#include "ntp_manager.h"

static bool     s_synced         = false; /**< true once ntp_manager_record_sync() has been called at least once. */
static uint32_t s_millis_at_sync = 0;      /**< millis() value at the moment of the most recent sync. */
static uint32_t s_epoch_at_sync  = 0;      /**< UTC epoch seconds corresponding to s_millis_at_sync. */

bool ntp_manager_should_sync(bool sta_connected, bool already_synced)
{
    return sta_connected && !already_synced;
}

/**
 * @brief Gregorian leap-year rule: divisible by 4, except centuries unless
 *        also divisible by 400 (so 2000 is a leap year, 2100 is not).
 * @param year Calendar year, e.g. 2026.
 * @return true if @p year is a leap year.
 */
static bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
}

bool ntp_manager_validate_manual_time(const manual_time_t *t)
{
    if (t->month < 1 || t->month > 12) {
        return false;
    }
    if (t->hour > 23 || t->minute > 59 || t->second > 59) {
        return false;
    }

    static const uint8_t days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t max_day = days_in_month[t->month - 1];
    if (t->month == 2 && is_leap_year(t->year)) {
        max_day = 29;
    }
    if (t->day < 1 || t->day > max_day) {
        return false;
    }
    return true;
}

void ntp_manager_record_sync(uint32_t millis_at_sync, uint32_t epoch_at_sync)
{
    s_synced         = true;
    s_millis_at_sync = millis_at_sync;
    s_epoch_at_sync  = epoch_at_sync;
}

bool ntp_manager_is_synced(void)
{
    return s_synced;
}

uint32_t ntp_manager_millis_to_epoch(uint32_t millis_value)
{
    if (!s_synced) {
        return 0;
    }
    /* Unsigned subtraction is correct modulo 2^32 even across a millis()
     * wraparound, as long as the true gap between sync and millis_value is
     * under ~24.8 days — comfortably true for a bench tool that gets
     * rebooted far more often than that. A millis_value from *before* the
     * recorded sync point has no meaningful epoch anyway (there was no
     * wall clock yet), so it's not handled specially — a stale/garbage
     * result there is a non-issue, not a bug. */
    uint32_t delta_ms = millis_value - s_millis_at_sync;
    return s_epoch_at_sync + (delta_ms / 1000);
}

void ntp_manager_reset(void)
{
    s_synced         = false;
    s_millis_at_sync = 0;
    s_epoch_at_sync  = 0;
}
