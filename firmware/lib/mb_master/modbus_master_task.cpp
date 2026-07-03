/**
 * @file modbus_master_task.cpp
 * @brief FreeRTOS task body: dequeue an mb_task_request_t, run it, reply.
 *
 * Everything here is queue plumbing and NVS timing lookup — the actual
 * request-processing behaviour lives in mb_master.cpp and is tested there
 * via `pio test -e native`. This file only builds under ARDUINO and has no
 * native test coverage of its own (see the file-level doc comment in
 * modbus_master_task.h for why that's an accepted gap).
 */
#ifdef ARDUINO
#include "modbus_master_task.h"
#include "cfg.h"
#include "cfg_keys.h"
#include <Arduino.h>

static QueueHandle_t s_request_queue = 0; /**< The task's inbound request queue; see modbus_master_get_queue(). */

/**
 * @brief Task body: forever loop of dequeue one request, apply timing, run it, reply.
 *
 * Blocks on xQueueReceive() with portMAX_DELAY, so it's fully idle between
 * requests. Per task_req.override_timing, either applies the one-shot
 * override values or re-reads mb_timeout_ms/mb_retries from NVS on *every*
 * iteration (not cached at task start) so a settings-page change takes
 * effect starting with the next dequeued request and never mid-request
 * (design/completeRealisationPlan.md TASK-MB). Only posts a reply if
 * reply_to is non-NULL, and does so with a zero tick timeout — a caller
 * that isn't ready to receive loses the result rather than stalling this
 * task (and therefore the whole bus) waiting on it.
 *
 * Takes the usual FreeRTOS `void *` task parameter, unused here and left
 * unnamed at the call site.
 */
static void modbus_master_task_fn(void * /*pvParameters*/)
{
    mb_task_request_t task_req;
    for (;;) {
        if (xQueueReceive(s_request_queue, &task_req, portMAX_DELAY) == pdTRUE) {
            if (task_req.override_timing) {
                /* design/api.md §4.1 per-request override — this transaction
                 * only, NVS untouched. */
                mb_set_timeout(task_req.timeout_override_ms);
                mb_set_retries(task_req.retries_override);
            } else {
                /* Re-read on every transaction, not just at startup — a
                 * settings-page change takes effect on the *next* request,
                 * never mid-request (design/completeRealisationPlan.md TASK-MB). */
                mb_set_timeout(cfg_get_u16(CFG_KEY_MB_TIMEOUT_MS, CFG_DEFAULT_MB_TIMEOUT_MS));
                mb_set_retries(cfg_get_u8(CFG_KEY_MB_RETRIES, CFG_DEFAULT_MB_RETRIES));
            }

            mb_result_t result = mb_master_process(&task_req.request, millis());

            if (task_req.reply_to != 0) {
                xQueueSend(task_req.reply_to, &result, 0);
            }
        }
    }
}

void modbus_master_task_start(void)
{
    s_request_queue = xQueueCreate(8, sizeof(mb_task_request_t));
    xTaskCreatePinnedToCore(modbus_master_task_fn, "modbus_master", 4096, NULL, 5, NULL, APP_CPU_NUM);
}

QueueHandle_t modbus_master_get_queue(void)
{
    return s_request_queue;
}

#endif /* ARDUINO */
