# Gotcha log — Windmeters Modbus Interface Tester

Surprising or non-obvious things learned while building this project.
Check here before debugging from scratch — mirrors the convention used in
the sibling `greenhouse-Controller` repo's own `memory/gotcha-log.md`.

## Hardware — M5Stack AtomS3 + Atomic RS485 Base

**RS485 base RX/TX pins are G5/G6, not G1/G2 — official docs are wrong for
this combo.** M5Stack's own docs page lists the `HY2.0-4P` Grove port on
plain AtomS3 as `G1`/`G2`/`5V`/`GND`. Bench-verified (loopback test) that
the Atomic RS485 Base actually lands on `G5`(RX)/`G6`(TX) instead. Trust
bench verification over the M5Stack docs page for this specific base+board
combo. See `design/scratchbook.md` §4.

**Plain "AtomS3" vs "AtomS3 Lite" naming is unreliable — check for the LED
empirically.** Plain AtomS3 (with the 0.85" LCD) is documented as having no
RGB LED; AtomS3 Lite (no LCD) has one at GPIO35. In practice, a working
test sketch (WS2812B, FastLED,
`Atom-RS485-Base-explorations/blinkyS3/main.cpp`) confirmed GPIO35 works on
the actual target hardware regardless of which retail SKU it technically
is — don't assume LED presence/absence from the SKU name alone if you can
just flash a blink test instead.

**`ARDUINO_USB_CDC_ON_BOOT` must be set explicitly — silent failure
otherwise.** The `m5stack-atoms3` board manifest sets `ARDUINO_USB_MODE=1`
but leaves `ARDUINO_USB_CDC_ON_BOOT` unset. Without
`-DARDUINO_USB_CDC_ON_BOOT=1` in `build_flags` (see `firmware/platformio.ini`),
`Serial.println()` silently goes to UART0 (unwired on this board) instead
of the native USB-CDC port used for flashing/monitoring — no error, no
crash, just total silence on the serial monitor while the firmware is
actually running fine. First thing to check if a freshly-flashed AtomS3
sketch produces zero serial output.

**UART loopback tests need an RX-buffer flush right after
`Serial2.begin()`.** Observed a single stray glitch byte sitting in the RX
buffer immediately after `begin()`, before any real data was sent — showed
up as a one-byte-shifted, otherwise-correct echo (looked like a wiring
fault at first glance, wasn't one). Flushing
(`while (Serial2.available()) Serial2.read();`) after `begin()` and before
sending fixed it. See `firmware/src/main.cpp`.

**The first real Modbus transaction(s) after a fresh boot can fail — ROOT
CAUSED AND FIXED 2026-07-02.** Confirmed to be exactly the hypothesis this
entry originally flagged: the UART-glitch-after-`begin()` stray byte above
was real, and its fix was never carried from the removed bring-up test
into the actual `mb_transport_arduino_init()` production path — that
function called `Serial2.begin()` and returned with zero flush, so the
stray byte sat queued indefinitely (nothing reads `Serial2` between boot
and the first real transaction) until the first scan's first read
consumed it as a fake leading byte, shifting every byte after it by one
position and corrupting that read's CRC — and often the next read too,
since the shift leaves one real byte behind for the *next* read to
mis-consume first. Manifests differently depending what's actually
listening: with a real bus peer (FG6485A) that DOES reply, the corrupted
read registers as a CRC error (or two, if the shift cascades into a second
real reply) rather than the `found:[]` this entry originally reported —
same root cause, `bus_scan`'s exception-still-counts-as-found path just
wasn't the thing garbled that time. Fixed by adding the flush
(`while (Serial2.available()) Serial2.read();`, after a short settle
delay) directly to `mb_transport_arduino_init()`. Verified by direct
before/after reproduction on real hardware: a fresh reset + immediate
narrow scan showed `crc_errors` go from 0→1 (unfixed) with the FG6485A's
real reply misread as a CRC error, then 0→0 (fixed) with that exact same
reply correctly parsed as `found`. If this class of symptom ever
reappears, suspect a *different* stray-byte source (not this one — it's
closed) before re-opening this entry.

## Firmware — NVS / `cfg` key names

**A `cfg_keys.h` key over Preferences' 15-character limit doesn't error —
it silently never persists, forever, and mock-backed native tests can't
catch it.** `CFG_KEY_SCAN_RANGE_START` was `"scan_range_start"` (16 chars)
and `CFG_KEY_WIND_POLL_INTERVAL` was `"wind_poll_interval_ms"` (21 chars) —
both well past ESP-IDF NVS's `NVS_KEY_NAME_MAX_SIZE - 1` = 15. Found via
INT-09 (`whatsNext.md`): set a distinctive scan range, rebooted the real
device, and the range silently reverted to the default instead of the
value just set. `cfg_backend_preferences.cpp`'s `prefs_set_u8()` (and
every sibling `prefs_set_*`) calls `Preferences::put*()` and discards the
return value — `put*()` itself returns 0/fails silently on an oversized
key rather than throwing or logging, so every subsequent `get*()` just
falls back to the default as if nothing had ever been written. Every
native test for these two keys (`test_cfg_round_trip_u32`,
`test_cfg_round_trip_u8`, `test_cfg_persists_across_reinit`) passed the
whole time — `mock_cfg_backend` is an in-memory map with no key-length
constraint, so it structurally cannot catch this class of bug. Fixed by
shortening both keys (`scan_start`/`scan_end`/`wind_poll_ms`) and adding
`test_all_keys_fit_the_15_char_preferences_limit` — a pure `strlen()`
check over every `CFG_KEY_*` constant, no backend needed, so this can
never silently recur. If a setting seems to "not stick" across a reboot
specifically (not a general read/write failure — writes and immediate
reads within the same boot session can still coincidentally look fine
depending on caching), check the key length first.

## Firmware — WebSocket live-update patterns

**Polling only "the single newest item" between broadcast ticks silently
drops anything else appended in between — even though it's still safely in
the source buffer.** The Modbus log's WebSocket broadcaster called
`mblog_get_recent(&entry, 1)` once per second and only ever pushed that one
entry to clients. `mb_master_process()` logs a TX entry immediately
followed by an RX entry sharing the *same* `timestamp_ms` — for a single
fast transaction (any one-off Register Explorer query), RX became "the
newest entry" before the broadcaster's next tick ever looked, so the TX
was skipped essentially every time. Not a data-loss bug — `mblog_append()`
always correctly stored both, so `GET /api/v1/log` (which reads the whole
ring buffer) was never affected — only the *live* WebSocket tail was
wrong, which made it easy to miss: the log looked complete on a fresh page
load, just quietly lost entries in real time. A bare timestamp comparison
can't fix this either, since TX/RX share one — needed a real monotonic
"total ever appended" counter (`mblog_total_appended()`) so the broadcaster
can compute exactly how many entries to catch up on, not just "is there
something new." General lesson: any periodic-poll broadcaster sitting on
top of a buffer that can receive more than one write per producer-call
needs to diff against a counter, not compare against "the current latest
value," or it will silently drop everything except whatever happened to be
newest at each poll instant.

**A hand-maintained constant describing a data shape (register maps, JSON
schemas, …) will drift the moment the real shape changes, with nothing to
catch it.** `web_server_task.cpp`'s `DUT_REGISTER_SNAPSHOT_JSON` — the
`GET /api/v1/spec` payload's `dut_register_snapshot` field — was a literal
string mirroring the wind register map, with its own comment saying
"update by hand if §5 changes." §5 changed (the 2026-07-02 speed/direction
split) and this constant didn't, because nothing forced it to; it kept
compiling and kept returning valid-looking JSON, just JSON describing
registers that no longer existed on either sensor type. No test caught it
either — it's a literal in Arduino-only code, outside this project's
tested/untested split (`web_core` is tested, `web_server_task.cpp`'s route
bodies aren't). Found only by manually re-deriving what the endpoint
*should* return while updating documentation, not by anything automated.
Worth a periodic manual check for any other hand-maintained "snapshot of
the real thing" constants in this codebase, since this class of drift is
structurally invisible to both the compiler and the test suite.

## Toolchain — HTTP verification from PowerShell

**`Invoke-WebRequest` without `-UseBasicParsing` can fail with "PowerShell
is in NonInteractive mode" even when the target server responded fine.**
Classic `Invoke-WebRequest` tries to parse the response through IE's DOM
engine for its `.Content`/`.ParsedHtml` conveniences, which needs
interactive COM components that aren't available in a non-interactive
script host — this looks exactly like a device/network reachability
failure but isn't one. Always add `-UseBasicParsing` when polling a device
over HTTP from this tool; a real "not reachable yet" retry loop should use
it from the first attempt, not just after ruling out the timing explanation.

## Toolchain — PlatformIO on this machine

The Bash tool's PATH has neither `pio` nor `g++`/`gcc` by default, but both
are installed locally:

- **PlatformIO Core**: `~/.platformio/penv/Scripts/pio.exe` (confirmed
  working, v6.1.19). Bundled Python venv, separate from system PATH.
- **C++ compiler**: `C:\Program Files\CodeBlocks\MinGW\bin\g++.exe`
  (MinGW-W64, GCC 14.2.0) — this is the toolchain `firmware/set_compiler.py`
  points PlatformIO's native test env at.

Before assuming "no compiler available," check these paths first — the
same lesson applies to any repo on this machine, not just this one
(cross-reference: greenhouse-Controller's own `drivers/*/platformio.ini`
use the identical `set_compiler.py` pattern).

**Setting up a PlatformIO project with both a hardware env and
`[env:native]`:** don't put `platform`/`framework` in a shared top-level
`[env]` block — `[env:native]` will inherit `framework = arduino` and
PlatformIO errors demanding a `board` for it (native has no board). Put
`platform`/`framework` in the hardware-specific env section instead (see
`firmware/platformio.ini`).

**`lib_ldf_mode = off` + a manual `include_lib.cpp` is only needed for a
specific PlatformIO 6 Windows lock-file bug with self-referencing
`lib_deps`.** A plain `lib/<name>/` library with no self-referencing
`lib_deps` entry works fine with PlatformIO's normal Library Dependency
Finder left on — confirmed by removing `lib_ldf_mode = off` and watching a
"unity.h not found" error disappear with zero other changes. Only reach for
that workaround if actually hitting the specific bug.

## Networking — WiFi/NTP/mDNS bring-up

**The WiFi driver's own event-log lines can arrive genuinely interleaved
in the serial output**, e.g. `AP_START`/`AP_STOP`/`AP_START` text
overlapping mid-character when `WiFi.mode(WIFI_MODE_APSTA)` +
`softAP()`/`begin()` fire close together. Looks alarming (like a UART
framing problem) but is normal ESP32 Arduino-core event-callback noise,
not a functional issue — confirmed by checking outcomes through an
independent channel (a real WiFi scan, `netsh wlan show networks`) instead
of trying to parse the garbled log more carefully.

**mDNS (`<hostname>.local`) can resolve on this dev machine even when its
own WiFi radio shows "disconnected"** (`netsh wlan show interfaces` reads
`State: disconnected`) — `Resolve-DnsName`/`ping` still found the AtomS3 at
its real DHCP IP, presumably reached via Ethernet + router bridging.
Don't assume "the PC isn't on WiFi" means mDNS checks from it are invalid.

**Provisioning a real secret (WiFi password) onto the device without
letting it touch the tracked source tree**: temporarily add a
`cfg_set_str(...)` block to `main.cpp`, flash once, confirm via serial
readback that it landed in NVS, then revert the source edit immediately —
NVS survives the next (credential-free) reflash, so the revert doesn't
lose anything. Verified this exact sequence works for getting
`wifi_ssid`/`wifi_pass` into NVS ahead of TASK-WEB's real settings page.

## Working approach that paid off

When a build/test tool isn't immediately available, search this machine
for an already-installed toolchain before assuming verification is
impossible. Doing that here turned "wrote code, hope it compiles" into
"compiled it, ran 21 real unit tests, flashed real hardware, found and
fixed two genuine bugs" (the `ARDUINO_USB_CDC_ON_BOOT` flag and the UART
glitch byte above) in one session. Neither bug would have been caught by
reading the code alone — only by actually running it and treating a FAIL
as something to diagnose, not route around.

Same principle paid off again verifying TASK-WIFI/TASK-NTP: rather than
leaving "STA connect / real NTP sync / mDNS" as permanently untestable
gaps, provisioning real credentials (see above) turned all three into
genuinely confirmed results — real SSID/BSSID/DHCP IP, a real NTP
timestamp overwriting a manually-set one, and a real `Resolve-DnsName` hit
matching the DHCP IP.

## Related

- Design source of truth: `design/scratchbook.md`
- Modbus master implementation plan: `design/realisationPlan.md`
- Whole-project implementation plan: `design/completeRealisationPlan.md`
- Current progress snapshot: `design/progress.md`
- Remaining phases (integration test, real-DUT verification, refinements): `design/whatsNext.md`
