/**
 * @file wifi_manager.h
 * @brief WiFi manager — the testable decision core (TASK-WIFI,
 *        design/completeRealisationPlan.md).
 *
 * The AP starts unconditionally at boot, before anything about
 * credentials is even checked — "so the device is never unreachable"
 * (completeRealisationPlan.md) means the AP has to be up for the whole
 * window where an STA attempt might still be in flight or might fail, not
 * just after the fact. What actually varies is: (1) whether an STA
 * connect attempt is worth making at all, and (2) once it resolves,
 * whether the AP should come back down.
 *
 * Those two decisions are pure functions — no WiFi radio calls — so
 * they're host-testable. Actually driving WiFi.softAP()/WiFi.begin() and
 * polling for the result is wifi_manager_task.cpp (Arduino-only, no tests
 * of its own — same division of labour as mb_master vs
 * modbus_master_task).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Is a stored SSID worth attempting an STA connection with?
 * @param stored_ssid wifi_ssid from NVS ("" or NULL if never configured).
 * @return true if @p stored_ssid is non-NULL and non-empty (an STA attempt
 *         is worth making); false otherwise.
 */
bool wifi_manager_should_attempt_sta(const char *stored_ssid);

/**
 * @brief Once an STA connect attempt has resolved, should the AP stay up?
 * @param sta_connected Whether the STA connect attempt succeeded.
 * @return true = keep the AP running (attempt failed, device must stay
 *         reachable somehow); false = safe to disable it.
 */
bool wifi_manager_should_keep_ap_after_connect(bool sta_connected);

/**
 * @brief Format the AP SSID: "WindmeterTester-XXYY", XX/YY = last two MAC
 *        bytes as uppercase hex — matches the template's SensorEmulator-AABB.
 * @param out        Destination buffer. Caller-owned; always null-terminated
 *                    within @p out_size (snprintf() semantics — truncated,
 *                    not overflowed, if too small).
 * @param out_size    Capacity of @p out; 22 bytes covers the longest possible
 *                    result ("WindmeterTester-XXYY" + NUL).
 * @param mac_byte4   Second-to-last byte of the station MAC address.
 * @param mac_byte5   Last byte of the station MAC address.
 */
void wifi_manager_format_ap_ssid(char *out, size_t out_size, uint8_t mac_byte4, uint8_t mac_byte5);
