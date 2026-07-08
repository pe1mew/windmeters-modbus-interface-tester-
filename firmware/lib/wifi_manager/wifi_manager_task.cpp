/**
 * @file wifi_manager_task.cpp
 * @brief FreeRTOS/Arduino orchestration around the wifi_manager decision
 *        core — implementation (TASK-WIFI, design/completeRealisationPlan.md).
 *
 * See wifi_manager_task.h for the thin-wrapper design rationale and
 * wifi_manager.h for why the AP starts unconditionally. This file owns the
 * state machine: AP up -> optionally attempt STA -> tear the AP down once
 * STA actually connects, all driven by the pure decisions in
 * wifi_manager.cpp.
 *
 * The initial connect attempt only busy-polls for STA_CONNECT_TIMEOUT_MS —
 * a slow AP/router negotiation or DHCP lease can easily take longer than
 * that, in which case WiFi.begin() is often still connecting in the
 * background even after this file stops watching for it. The idle loop
 * below keeps checking at a slower cadence so a late-landing connection
 * still tears the AP down instead of leaving it up for the rest of the
 * boot (found on real hardware: a client had genuinely joined via STA, but
 * the AP was still broadcasting because the 15 s window had already
 * expired when it landed).
 *
 * Still no reconnect-on-drop: once STA has connected and the AP is torn
 * down, a *later* disconnect does not bring the AP back — that remains
 * out of scope for this pass.
 */
#ifdef ARDUINO
#include "wifi_manager_task.h"
#include "wifi_manager.h"
#include "cfg.h"
#include "cfg_keys.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
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
 *
 * Forces 20 MHz (HT20) channel width rather than the driver's 40 MHz (HT40)
 * default — narrower is friendlier to a crowded 2.4 GHz bench/lab
 * environment (a 40 MHz AP occupies two 20 MHz channels' worth of
 * spectrum, overlapping more neighbouring networks) and this AP's job is
 * reachability, not throughput. Must run after WiFi.mode() has started the
 * driver but before WiFi.softAP() actually begins broadcasting, so the
 * narrower width is in effect from the AP's first beacon.
 */
static void start_ap(void)
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[33];
    wifi_manager_format_ap_ssid(ssid, sizeof(ssid), mac[4], mac[5]);

    esp_err_t bw_err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    wifi_bandwidth_t bw = WIFI_BW_HT40;
    esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
    Serial.printf("AP bandwidth: requested 20MHz (set result %d), now %s\n",
                  bw_err, bw == WIFI_BW_HT20 ? "20MHz" : "40MHz");

    WiFi.softAP(ssid); /* open, no password — matches the template's convention */

    strncpy(s_status.ssid, ssid, sizeof(s_status.ssid) - 1);
    s_status.ap_active = true;
}

/**
 * @brief Record a freshly-confirmed STA connection's SSID/IP/RSSI into s_status.
 * @param ssid SSID that just connected; copied into s_status.ssid.
 */
static void apply_sta_connected_status(const char *ssid)
{
    strncpy(s_status.ssid, ssid, sizeof(s_status.ssid) - 1);
    IPAddress ip = WiFi.localIP();
    snprintf(s_status.ip, sizeof(s_status.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    s_status.rssi = (int8_t)WiFi.RSSI();
    s_status.sta_connected = true;
}

/**
 * @brief Tear down the AP now that STA is confirmed connected, and register mDNS.
 *
 * Idempotent in effect but not meant to be called twice — callers gate on
 * their own "already torn down" flag (see wifi_manager_task_fn()) so this
 * only ever runs once per boot.
 */
static void teardown_ap_for_sta(void)
{
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_MODE_STA);
    s_status.ap_active = false;
    strncpy(s_status.mode_str, "STA", sizeof(s_status.mode_str) - 1);
    MDNS.begin(MDNS_HOSTNAME);
}

/**
 * @brief Connectivity state machine: AP up, optionally attempt STA, tear
 *        the AP down whenever STA actually connects (even late), then idle.
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
 *   4. wifi_manager_should_keep_ap_after_connect() makes the initial call:
 *      on success the AP is torn down immediately; on failure the AP is
 *      left up (mode_str reverts to plain "AP") so the device stays
 *      reachable, per TASK-WIFI's "never unreachable" requirement — but
 *      WiFi.begin() may still be quietly retrying underneath, so this
 *      isn't necessarily final (see the idle loop, below).
 *   5. If the AP is still up after step 4 and an attempt was made, the
 *      idle loop below keeps polling WiFi.status() every 5 s and tears
 *      the AP down the moment a late connection lands, rather than
 *      leaving it up for the rest of the boot.
 *
 * Runs for the lifetime of the task — steps 1-4 execute once, then the
 * function parks in the poll loop (no reconnect-on-drop after the AP is
 * torn down — a later disconnect doesn't bring it back, still out of
 * scope for this pass).
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

    bool sta_attempted = wifi_manager_should_attempt_sta(stored_ssid);
    bool ap_torn_down  = false;

    if (sta_attempted) {
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
            apply_sta_connected_status(stored_ssid);
        }

        if (!wifi_manager_should_keep_ap_after_connect(connected)) {
            teardown_ap_for_sta();
            ap_torn_down = true;
        } else {
            strncpy(s_status.mode_str, "AP", sizeof(s_status.mode_str) - 1);
        }
    }

    for (;;) {
        /* Catches a late-landing STA connection: WiFi.begin() can still be
         * connecting in the background after STA_CONNECT_TIMEOUT_MS gave
         * up watching it (slow AP/router negotiation, DHCP delay). Without
         * this, the AP would stay up for the rest of the boot even once a
         * client is genuinely connected via STA. Stops checking once torn
         * down — no reconnect-on-drop after that, a later disconnect does
         * not bring the AP back (still out of scope for this pass). */
        if (!ap_torn_down && sta_attempted && WiFi.status() == WL_CONNECTED) {
            apply_sta_connected_status(stored_ssid);
            teardown_ap_for_sta();
            ap_torn_down = true;
        }
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
