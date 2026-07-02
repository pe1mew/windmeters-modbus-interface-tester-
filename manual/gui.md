# Web UI — Overview

The Windmeters Modbus Interface Tester is operated entirely through a
built-in web page served by the AtomS3 itself — no app install, no
companion software. This page explains how to reach it and how it's laid
out; the individual sections each have their own manual (see the list at
the bottom).

## Connecting

- **First boot, or no WiFi configured yet:** the device starts its own
  access point, `WindmeterTester-<last 2 MAC bytes>` (open, no password).
  Join it and browse to `http://192.168.4.1`.
- **After WiFi is configured** ([System Settings](systemSettings.md)): the
  device joins your network instead and the AP is disabled. Reach it at
  either `http://windmeter-tester.local` (mDNS) or the IP address your
  router assigned it.
- The page footer shows the firmware version currently running on the
  device (e.g. `v0.4.0`) once the first status update arrives — handy for
  confirming an OTA/reflash actually landed the build you expected.
- The badge in the top-right of the header shows **Online**/**Offline** —
  this reflects the page's WebSocket connection to the device, not the
  device's own WiFi state (that's the WiFi card in Status, below). If it
  says Offline, the page will keep retrying every few seconds; a manual
  reload also works once the device is reachable again.

## Page layout

The page has three fixed regions, top to bottom:

1. **Status** — always visible, never scrolls out of view behind a tab.
2. **Tab bar** — Bus Scanner, Wind Speed, Wind Direction, Register
   Explorer, System Settings. Click a tab to switch; the others' state
   (e.g. a running scan) keeps running in the background even while you're
   not looking at that tab.
3. **Modbus Log** — always visible, pinned to the bottom, below the tabs.

This means Status and the Log are always on screen regardless of which
tab is open — useful when, say, you're watching bus health counters while
driving a scan.

## Status

Four cards, updated roughly once a second over the WebSocket connection:

| Card | Shows |
|---|---|
| WiFi | Mode (AP/STA), SSID, IP address, signal strength (RSSI, dBm) |
| Clock | Local date/time, and an **NTP synced** / **NTP pending** badge |
| Modbus Bus | CRC error count, timeout count, and the last Modbus exception code seen (or "none") — cumulative since boot |
| Uptime | Time since the device last started, `HH:MM:SS` |

## Modbus Log

A scrolling table of every Modbus frame the tester has sent or received
(from any tab, or from the [API](api.md)) — see its own section below for
detail, or just note here: timestamps are `HH:MM:SS`, the table shows the
last 50 frames, and **Clear** empties it.

| Column | Meaning |
|---|---|
| Time | `HH:MM:SS` — wall-clock local time once NTP is synced, device uptime otherwise |
| Dir | `TX` (tester sent this) or `RX` (tester received this) |
| Frame (hex) | The exact bytes on the wire, including the CRC |
| Summary | One-line decode, e.g. `FC04 addr31 start0x0000 cnt2 -> OK` |

Column widths can be dragged to resize (hover the right edge of a header
cell for the resize handle).

## The tabs, in detail

- [Bus Scanner](busScanner.md) — sweep an address range and see what
  responds.
- [Wind Speed / Wind Direction](windTesters.md) — live decode and
  configuration of the two wind sensor units.
- [Register Explorer](registerExplorer.md) — a manual, one-shot
  read/write tool for any register on any address.
- [System Settings](systemSettings.md) — WiFi, NTP, manual time, and
  Modbus timeout/retries.
- [Machine API](api.md) — everything above is also reachable over plain
  JSON-over-HTTP, for scripts or an LLM driving the tester without a
  browser.
