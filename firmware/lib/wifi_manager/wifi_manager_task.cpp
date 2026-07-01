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

#define STA_CONNECT_TIMEOUT_MS 15000u
#define MDNS_HOSTNAME           "windmeter-tester"

static wifi_status_t s_status;

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
