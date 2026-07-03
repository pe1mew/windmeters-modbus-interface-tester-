/**
 * @file wind_poll_task.h
 * @brief FreeRTOS/Arduino orchestration around the wind_poll core
 *        (TASK-WIND, design/completeRealisationPlan.md).
 *
 * Thin by the same design as everywhere else in this project: decode/encode
 * math lives in wind_poll.h/.cpp and is tested there. This file periodically
 * submits FC04 reads to modbus_master_task's queue while active, and
 * exposes on-demand config read (bulk FC03) / write (single-field FC06) —
 * both of those submit through the same queue too, so they're safe to call
 * from any task, not just this one. No tests of its own, Arduino-only.
 */
#pragma once

#ifdef ARDUINO

#include "wind_poll.h"

/**
 * @brief All 4 holding registers, decoded (TDS §2.8) — same 4 fields at
 * the same addresses regardless of sensor type (FR-MB27); which ones are
 * functionally meaningful for a given build is a GUI-layer concern now,
 * not a wire-protocol one (unlike before TDS v0.6, when the register
 * *addresses* themselves depended on type). There is no device_addr field
 * — that register no longer exists (FR-MB07/FR-MB26).
 */
typedef struct {
    float    dir_offset_deg;        /**< Holding reg 0x0000 (40001), 0.1°. */
    uint16_t measurement_window_ms; /**< Holding reg 0x0001 (40002), ms. */
    uint16_t averaging_window_s;    /**< Holding reg 0x0002 (40003), s. */
    float    low_speed_cutoff_ms;   /**< Holding reg 0x0003 (40004), 0.1 m/s. */
} wind_config_t;

/** @brief Start the task. modbus_master_task_start() must already have run. */
void wind_poll_task_start(void);

/**
 * @brief Start (or retarget) polling @p addr as sensor @p type, or stop
 * entirely. Only one target is ever active at a time (scratchbook.md §9 —
 * multi-device dashboard deferred to v2) — starting a WindSpeed poll while
 * WindDirection is active stops WindDirection, and vice versa.
 * @param addr   Slave address to poll, 1-247. Ignored (but still stored)
 *               when @p active is false.
 * @param type   Sensor type @p addr is built as; selects which fields of
 *               the decoded reading are meaningful — see wind_poll_decode().
 * @param active false suspends polling — zero bus traffic from this task
 *               until re-activated (completeRealisationPlan.md TASK-WIND).
 */
void wind_poll_set_active(uint8_t addr, wind_sensor_type_t type, bool active);

/** @brief Is the task currently polling a target? @return The active flag last set via wind_poll_set_active(). */
bool wind_poll_is_active(void);

/** @brief Sensor type of the current (or most recent) active target — only meaningful when wind_poll_is_active() or wind_poll_has_data(). @return Value last passed to wind_poll_set_active() as @p type. */
wind_sensor_type_t wind_poll_get_active_type(void);

/** @brief Most recent successfully decoded reading (stale data kept on a failed poll, not cleared). @return A copy of the last decoded wind_reading_t; all-zero/false if wind_poll_has_data() is false. */
wind_reading_t wind_poll_get_latest(void);

/** @brief Has at least one successful poll ever happened? @return true once the first successful poll completes; never resets back to false. */
bool wind_poll_has_data(void);

/** @brief Milliseconds since the last successful poll, or UINT32_MAX if wind_poll_has_data() is false. @return Elapsed time in ms, or UINT32_MAX as the "no data yet" sentinel. */
uint32_t wind_poll_age_ms(void);

/**
 * @brief Bulk FC03 read of all 4 holding registers, decoded (TDS §2.8).
 * No @p type parameter — the register block is identical regardless of
 * sensor type as of TDS v0.6 (FR-MB27). Blocks on the real transaction.
 * @param addr Slave address to read, 1-247.
 * @param out  Destination for the decoded config. Caller-owned; only
 *             written on success — left untouched if this returns false.
 * @return true if the FC03 transaction succeeded and @p out was populated;
 *         false on timeout/CRC/framing/exception (no partial writes to
 *         @p out in that case).
 */
bool wind_poll_read_config(uint8_t addr, wind_config_t *out);

/**
 * @brief FC06 write of just @p field's register — never a blind write of
 * the whole block, so a typo in one field can't clobber the others. No
 * @p type parameter, same reason as wind_poll_read_config(). Blocks on
 * the real transaction.
 * @param addr  Slave address to write, 1-247.
 * @param field Which single holding register to write.
 * @param value Engineering-unit value; encoded via wind_config_field_encode()
 *              before being sent, not range-checked here.
 * @return true if the FC06 transaction succeeded (slave echoed the write
 *         back unchanged); false on timeout/CRC/framing/exception.
 */
bool wind_poll_write_config_field(uint8_t addr, wind_config_field_t field, float value);

#endif /* ARDUINO */
