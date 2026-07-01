/**
 * @file scan_task.h
 * @brief FreeRTOS/Arduino orchestration around the bus_scan core
 *        (TASK-SCAN, design/completeRealisationPlan.md).
 *
 * Thin by the same design as modbus_master_task.cpp: the actual state
 * machine lives in bus_scan.h/.cpp and is tested there. This file waits
 * for start/cancel commands, submits one probe per address to
 * modbus_master_task's queue (never touches the UART itself — same
 * single-bus-owner rule as scan_task's future siblings), and drives
 * LIB-LED's scanning/idle states.
 */
#pragma once

#ifdef ARDUINO

#include "bus_scan.h"

/**
 * @brief Start the task. modbus_master_task_start() must already have run.
 */
void scan_task_start(void);

/** @brief Ask the task to begin a sweep from @p range_start to @p range_end. Non-blocking. */
void scan_task_request_start(uint8_t range_start, uint8_t range_end);

/** @brief Ask the task to stop an in-progress sweep. Non-blocking, no-op if idle. */
void scan_task_request_cancel(void);

#endif /* ARDUINO */
