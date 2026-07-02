# System Settings

WiFi, time, and Modbus bus configuration — everything that's about the
tester itself rather than a specific test.

## WiFi

| Field | Meaning |
|---|---|
| SSID | Network name to join (up to 32 characters) |
| Password | Network password (up to 63 characters) |
| Connect | Saves the credentials and reconnects |

Clicking **Connect** saves the credentials to flash (NVS) and reboots the
device to apply them immediately — you don't need to power-cycle it
yourself. Once it reconnects in STA mode, **the access point is disabled**;
if you were connected via the AP (`WindmeterTester-....` at
`192.168.4.1`), that connection will drop. Reconnect to your normal
network and reach the tester at `http://windmeter-tester.local` or
whatever IP your router assigned it — check your router's client list if
mDNS doesn't resolve.

If the credentials are wrong or the network isn't reachable, the device
falls back to AP mode again rather than getting stuck unreachable — you
can always get back in via `192.168.4.1` to try again.

## NTP

| Field | Meaning |
|---|---|
| NTP server | Hostname to sync time from (default `pool.ntp.org`) |
| Set NTP server | Saves the server and triggers a sync attempt |

Requires STA mode with internet access to actually reach the server. The
**Clock** card in [Status](gui.md#status) shows **NTP synced** once
successful; until then it shows **NTP pending** and the clock runs from
whatever was last set manually (or from device uptime, if nothing was ever
set).

## Manual time

| Field | Meaning |
|---|---|
| Date & time | Sets the device's clock directly, interpreted as **UTC** |
| Set time | Applies it |

Useful for bench work with no internet reachable at all — the Modbus Log's
timestamps and the Status clock will use this until either a reboot or a
successful NTP sync overwrites it. Note the UTC framing: if your bench is
not in UTC, convert before typing it in.

## Modbus

| Field | Meaning |
|---|---|
| Timeout (ms) | How long to wait for a reply before giving up on an attempt, minimum 50 |
| Retries | How many additional attempts after the first one times out, 0–5 |
| Apply | Saves both values |

These fields **pre-populate automatically** from the device's current
configuration when the page loads — they're never blank by default, so
what you see when you open this tab is what the tester is actually using
right now, not a form waiting to be filled in. Changing them here affects
every Modbus transaction from then on (scans, wind polling, Register
Explorer, and the [API](api.md)) — unless a specific API call overrides
`timeout_ms`/`retries` for just that one request, which does not change
these saved settings.

A longer timeout and more retries make the tester more tolerant of a slow
or flaky bus, at the cost of a slower [Bus Scanner](busScanner.md) sweep
(each unanswered address now takes longer to give up on) and slower
failure reporting everywhere else.
