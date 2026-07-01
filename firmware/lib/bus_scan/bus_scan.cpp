#include "bus_scan.h"
#include <string.h>

static bus_scan_status_t s_status;

bool bus_scan_did_respond(mb_status_t status)
{
    return status == MB_OK || status == MB_ERR_EXCEPTION;
}

void bus_scan_start(uint8_t range_start, uint8_t range_end)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state         = SCAN_RUNNING;
    s_status.range_start   = range_start;
    s_status.range_end     = range_end;
    s_status.current_addr  = range_start;
}

void bus_scan_cancel(void)
{
    if (s_status.state == SCAN_RUNNING) {
        s_status.state = SCAN_CANCELLED;
    }
}

bool bus_scan_is_active(void)
{
    return s_status.state == SCAN_RUNNING;
}

void bus_scan_record_probe_result(bool responded)
{
    if (s_status.state != SCAN_RUNNING) {
        return;
    }

    if (responded && s_status.found_count < BUS_SCAN_MAX_FOUND) {
        s_status.found[s_status.found_count++] = s_status.current_addr;
    }

    if (s_status.current_addr >= s_status.range_end) {
        s_status.state = SCAN_COMPLETE;
    } else {
        s_status.current_addr++;
    }
}

bus_scan_status_t bus_scan_get_status(void)
{
    return s_status;
}
