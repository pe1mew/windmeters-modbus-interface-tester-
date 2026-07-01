#include "wifi_manager.h"
#include <stdio.h>

bool wifi_manager_should_attempt_sta(const char *stored_ssid)
{
    return stored_ssid != 0 && stored_ssid[0] != '\0';
}

bool wifi_manager_should_keep_ap_after_connect(bool sta_connected)
{
    return !sta_connected;
}

void wifi_manager_format_ap_ssid(char *out, size_t out_size, uint8_t mac_byte4, uint8_t mac_byte5)
{
    snprintf(out, out_size, "WindmeterTester-%02X%02X", mac_byte4, mac_byte5);
}
