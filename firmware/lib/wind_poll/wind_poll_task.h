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

typedef struct {
    uint8_t  device_addr;           /**< Holding reg 0x0000. */
    float    dir_offset_deg;        /**< Holding reg 0x0001, decoded. */
    uint16_t measurement_window_ms; /**< Holding reg 0x0002. */
    uint16_t averaging_window_s;    /**< Holding reg 0x0003. */
} wind_config_t;

/** @brief Start the task. modbus_master_task_start() must already have run. */
void wind_poll_task_start(void);

/**
 * @brief Start (or retarget) polling @p addr, or stop entirely.
 * @param active false suspends polling — zero bus traffic from this task
 *               until re-activated (completeRealisationPlan.md TASK-WIND).
 */
void wind_poll_set_active(uint8_t addr, bool active);

bool wind_poll_is_active(void);

/** @brief Most recent successfully decoded reading (stale data kept on a failed poll, not cleared). */
wind_reading_t wind_poll_get_latest(void);

/** @brief Has at least one successful poll ever happened? */
bool wind_poll_has_data(void);

/** @brief Milliseconds since the last successful poll, or UINT32_MAX if wind_poll_has_data() is false. */
uint32_t wind_poll_age_ms(void);

/** @brief Bulk FC03 read of holding registers 0x0000-0x0003, decoded. Blocks on the real transaction. */
bool wind_poll_read_config(uint8_t addr, wind_config_t *out);

/** @brief FC06 write of just @p field's register — never a blind write of all four. Blocks on the real transaction. */
bool wind_poll_write_config_field(uint8_t addr, wind_config_field_t field, float value);

#endif /* ARDUINO */
