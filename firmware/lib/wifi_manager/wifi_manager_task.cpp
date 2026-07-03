/**
 * @file wifi_manager_task.cpp
 * @brief FreeRTOS/Arduino orchestration around the wifi_manager decision
 *        core — implementation (TASK-WIFI, design/completeRealisationPlan.md).
 *
 * See wifi_manager_task.h for the thin-wrapper design rationale and
 * wifi_manager.h for why the AP starts unconditionally. This file owns the
 * one-shot state machine: AP up -> optionally attempt STA -> optionally
 * tear the AP down, all driven by the pure decisions in wifi_manager.cpp.
 * No periodic reconnect-on-drop yet (see the task loop below) — that's
 * intentionally out of scope for this pass.
 */
#ifdef ARDUINO
#include "wifi_manager_task.h"
#include "wifi_manager.h"
#include "cfg.h"
#include "cfg_keys.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <string.h>
#include <stdio.h>

#define STA_CONNECT_TIMEOUT_MS 15000u          /**< How long to wait for WiFi.begin() to reach WL_CONNECTED before falling back to AP-only. */
#define MDNS_HOSTNAME           "windmeter-tester" /**< Advertised as windmeter-tester.local once STA connects. */

static wifi_status_t s_status; /**< Current snapshot returned by wifi_manager_get_status(). */

/**
 * @brief Bring up the AP radio with the MAC-derived SSID and record it in
 *        s_status. Always open (no password) — matches the template's
 *        convention, since the AP's job is to guarantee reachability, not
 *        to be secured.
 */
static void start_ap(void)
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[33];
    wifi_manager_format_ap_ssid(ssid, sizeof(ssid), mac[4], mac[5]);

    WiFi.softAP(ssid); /* open, no password — matches the template's convention */

    strncpy(s_status.ssid, ssid, sizeof(s_status.ssid) - 1);
    s_status.ap_active = true;
}

/**
 * @brief One-shot connectivity state machine, then an idle loop forever.
 *
 * Sequence (order matters — this is the actual TASK-WIFI behaviour, not
 * just an implementation detail):
 *   1. AP goes up unconditionally, before stored credentials are even
 *      read — see wifi_manager.h's header comment for why this ordering
 *      is load-bearing (the AP must already be reachable for the whole
 *      window where an STA attempt might still be in flight or fail).
 *   2. wifi_manager_should_attempt_sta() decides whether stored_ssid is
 *      worth trying at all. If not, the function falls straight through
 *      to the idle loop and stays in plain AP mode forever.
 *   3. If an attempt is made, this busy-polls WiFi.status() for up to
 *      STA_CONNECT_TIMEOUT_MS. There is no distinction between "bad
 *      password", "AP out of range", and "still negotiating" — all of
 *      them just read as "not connected by the deadline".
 *   4. wifi_manager_should_keep_ap_after_connect() makes the final call:
 *      on success the AP is torn down and mDNS is registered; on failure
 *      the AP is left up (mode_str reverts to plain "AP") so the device
 *      stays reachable, per TASK-WIFI's "never unreachable" requirement.
 *
 * Runs for the lifetime of the task — steps 1-4 execute once, then the
 * function parks in a slow poll loop (no reconnect-on-drop in this pass).
 */
static void wifi_manager_task_fn(void * /*pvParameters*/)
{
    char stored_ssid[33] = {0};
    char stored_pass[65] = {0};
    cfg_get_str(CFG_KEY_WIFI_SSID, stored_ssid, sizeof(stored_ssid), CFG_DEFAULT_WIFI_SSID);
    cfg_get_str(CFG_KEY_WIFI_PASS, stored_pass, sizeof(stored_pass), CFG_DEFAULT_WIFI_PASS);

    /* AP starts unconditionally, before anything about credentials is
     * checked — see wifi_manager.h's header comment for why. */
    WiFi.mode(WIFI_MODE_APSTA);
    start_ap();
    strncpy(s_status.mode_str, "AP", sizeof(s_status.mode_str) - 1);

    if (wifi_manager_should_attempt_sta(stored_ssid)) {
        strncpy(s_status.mode_str, "AP+STA", sizeof(s_status.mode_str) - 1);
        WiFi.begin(stored_ssid, stored_pass);

        uint32_t start_ms = millis();
        bool connected = false;
        while ((millis() - start_ms) < STA_CONNECT_TIMEOUT_MS) {
            if (WiFi.status() == WL_CONNECTED) {
                connected = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        s_status.sta_connected = connected;

        if (connected) {
            strncpy(s_status.ssid, stored_ssid, sizeof(s_status.ssid) - 1);
            IPAddress ip = WiFi.localIP();
            snprintf(s_status.ip, sizeof(s_status.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            s_status.rssi = (int8_t)WiFi.RSSI();
        }

        if (!wifi_manager_should_keep_ap_after_connect(connected)) {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_MODE_STA);
            s_status.ap_active = false;
            strncpy(s_status.mode_str, "STA", sizeof(s_status.mode_str) - 1);
            MDNS.begin(MDNS_HOSTNAME);
        } else {
            strncpy(s_status.mode_str, "AP", sizeof(s_status.mode_str) - 1);
        }
    }

    for (;;) {
        /* Phase 2 doesn't reconnect-on-drop yet — that's a refinement for
         * a later pass, not part of this task's initial scope. */
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void wifi_manager_task_start(void)
{
    memset(&s_status, 0, sizeof(s_status));
    xTaskCreatePinnedToCore(wifi_manager_task_fn, "wifi_manager", 4096, NULL, 4, NULL, APP_CPU_NUM);
}

wifi_status_t wifi_manager_get_status(void)
{
    return s_status;
}

#endif /* ARDUINO */
