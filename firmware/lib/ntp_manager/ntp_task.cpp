/**
 * @file ntp_task.cpp
 * @brief FreeRTOS/Arduino orchestration around the ntp_manager decision/
 *        validation core — implementation (TASK-NTP,
 *        design/completeRealisationPlan.md).
 *
 * See ntp_task.h for the thin-wrapper design rationale and ntp_manager.h
 * for the millis()-vs-epoch scope note. This file owns the polling loop
 * that waits for wifi_manager to report an STA connection, then drives
 * configTime() and waits for the clock to actually land (configTime()
 * kicks off an async sync; it does not block until the time is valid).
 */
#ifdef ARDUINO
#include "ntp_task.h"
#include "wifi_manager_task.h"
#include "cfg.h"
#include "cfg_keys.h"
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define NTP_POLL_INTERVAL_MS     2000u      /**< How often the task checks whether should_sync()/landed-sync conditions changed. */
#define NTP_SYNC_WAIT_TIMEOUT_MS 10000u     /**< How long to poll for configTime() to land before giving up on this attempt. */
#define NTP_SANITY_EPOCH_FLOOR   1700000000u /**< ~Nov 2023 — anything before this means sync hasn't really landed. */

/**
 * @brief Poll loop: wait for wifi_manager to report STA-connected, then
 *        attempt one NTP sync and record it.
 *
 * Runs forever at NTP_POLL_INTERVAL_MS. Each iteration re-checks
 * ntp_manager_should_sync() — since that's "connected && !already_synced",
 * once s_synced flips true (via ntp_manager_record_sync() below, or via a
 * manual ntp_set_manual_time() call from the web UI) this loop becomes a
 * permanent no-op, matching TASK-NTP's "sync once" v1 scope.
 *
 * configTime() itself is async — it kicks off the SNTP client but returns
 * immediately — so a real sync is detected indirectly: poll time(0) until
 * it clears NTP_SANITY_EPOCH_FLOOR (a plausible "this is a real date, not
 * the 1970 epoch default") or NTP_SYNC_WAIT_TIMEOUT_MS elapses. On timeout
 * this iteration simply gives up silently; the next poll interval tries
 * again since should_sync() is still true.
 */
static void ntp_task_fn(void * /*pvParameters*/)
{
    for (;;) {
        wifi_status_t wifi = wifi_manager_get_status();
        if (ntp_manager_should_sync(wifi.sta_connected, ntp_manager_is_synced())) {
            char server[64];
            cfg_get_str(CFG_KEY_NTP_SERVER, server, sizeof(server), CFG_DEFAULT_NTP_SERVER);
            configTime(0, 0, server); /* UTC, no DST — narrower scope than the template, see ntp_manager.h */

            uint32_t start = millis();
            while ((millis() - start) < NTP_SYNC_WAIT_TIMEOUT_MS) {
                time_t now = time(0);
                if ((uint32_t)now > NTP_SANITY_EPOCH_FLOOR) {
                    ntp_manager_record_sync(millis(), (uint32_t)now);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(250));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(NTP_POLL_INTERVAL_MS));
    }
}

void ntp_task_start(void)
{
    xTaskCreatePinnedToCore(ntp_task_fn, "ntp_task", 4096, NULL, 3, NULL, APP_CPU_NUM);
}

bool ntp_is_synced(void)
{
    return ntp_manager_is_synced();
}

bool ntp_set_manual_time(const manual_time_t *t)
{
    if (!ntp_manager_validate_manual_time(t)) {
        return false;
    }

    struct tm tm_val;
    memset(&tm_val, 0, sizeof(tm_val));
    tm_val.tm_year = t->year - 1900;
    tm_val.tm_mon  = t->month - 1;
    tm_val.tm_mday = t->day;
    tm_val.tm_hour = t->hour;
    tm_val.tm_min  = t->minute;
    tm_val.tm_sec  = t->second;

    time_t epoch = mktime(&tm_val); /* no TZ is set anywhere, so this is effectively UTC in, UTC out */
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, 0);

    ntp_manager_record_sync(millis(), (uint32_t)epoch);
    return true;
}

#endif /* ARDUINO */
