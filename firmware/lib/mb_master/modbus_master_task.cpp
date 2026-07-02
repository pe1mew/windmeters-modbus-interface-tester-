#ifdef ARDUINO
#include "modbus_master_task.h"
#include "cfg.h"
#include "cfg_keys.h"
#include <Arduino.h>

static QueueHandle_t s_request_queue = 0;

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
