/**
 * @file ntp_task.h
 * @brief FreeRTOS/Arduino orchestration around the ntp_manager core
 *        (TASK-NTP, design/completeRealisationPlan.md).
 *
 * Thin by the same design as modbus_master_task.cpp / wifi_manager_task.cpp:
 * the actual decisions (should we sync? is this manual date valid?) live in
 * ntp_manager.h/.cpp and are tested there. This file just drives
 * configTime()/settimeofday() and polls wifi_manager's status — no tests
 * of its own, Arduino/FreeRTOS-only.
 */
#pragma once

#ifdef ARDUINO

#include "ntp_manager.h"

/**
 * @brief Start the polling task. cfg_init() and wifi_manager_task_start()
 *        must already have run — this reads ntp_server from NVS and polls
 *        wifi_manager_get_status() for an STA connection.
 */
void ntp_task_start(void);

/**
 * @brief Thin wrapper over ntp_manager_is_synced() — see there for meaning.
 * @return true once a sync (real NTP or manual) has landed.
 */
bool ntp_is_synced(void);

/**
 * @brief Apply a manually-entered date/time (web UI fallback path).
 *
 * Validates via ntp_manager_validate_manual_time(), then — only if
 * valid — calls settimeofday() and records the new reference point via
 * ntp_manager_record_sync(), same as a real NTP sync landing. No TZ is
 * configured anywhere in this project, so @p t is treated as UTC in,
 * UTC out.
 * @param t Candidate value; not modified. Must not be NULL.
 * @return false if @p t fails validation — nothing is applied in that case.
 */
bool ntp_set_manual_time(const manual_time_t *t);

#endif /* ARDUINO */
