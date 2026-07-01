/**
 * @file modbus_master_task.h
 * @brief FreeRTOS wrapper around mb_master_process() — the actual MB-2 task.
 *
 * design/realisationPlan.md §3: this task is the sole owner of the UART.
 * scan_task, wind_poll_task, and the Register Explorer (none of which
 * exist yet) will all submit requests here instead of calling mb_core
 * directly. Deliberately thin — mb_master.cpp already has and tests all
 * the actual behaviour (dispatch, logging, LED, health counters); this
 * file is just the queue plumbing, which is why it's Arduino/FreeRTOS-only
 * and has no native tests of its own (see memory/gotcha-log.md's
 * "verify before declaring blocked" note on where the coverage boundary
 * actually is).
 */
#pragma once

#ifdef ARDUINO

#include "mb_master.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

typedef struct {
    mb_request_t  request;
    /** Caller-owned queue (capacity >= 1, holds one mb_result_t) the task posts the result to. */
    QueueHandle_t reply_to;
} mb_task_request_t;

/**
 * @brief Create the request queue and start the task.
 *
 * mb_init() (with a real transport) and cfg_init() must already have been
 * called — this task reads Modbus timeout/retry settings from cfg on every
 * transaction (design/completeRealisationPlan.md TASK-MB).
 */
void modbus_master_task_start(void);

/** @brief Queue to xQueueSend() an mb_task_request_t to. NULL until modbus_master_task_start() runs. */
QueueHandle_t modbus_master_get_queue(void);

#endif /* ARDUINO */
