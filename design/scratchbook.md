# Scratchbook — Windmeters Modbus Interface Tester

Status: **draft / working notes** — 2026-07-01 (updated 2026-07-02: wind
speed/direction split, GUI restructure)

This is a scratchbook, not a frozen spec — it captures current thinking so
implementation can start, and it gets edited as decisions change. Mirrors the
spirit of the DUT's own `design/scratchBook.md`.

## 1. What this thing is

A standalone Modbus **master** bench tool used to develop and validate
[`pe1mew/windmeters-modbus-interface`](https://github.com/pe1mew/windmeters-modbus-interface)
(a CH32V003-based board that bridges a cup-anemometer + wind-vane potentiometer
onto Modbus RTU). Runs on an M5Stack AtomS3 + Atomic RS485 Base, forked from
the non-Modbus scaffolding of
[`pe1mew/greenhouse-Controller-Modbus-sensor-emulator`](https://github.com/pe1mew/greenhouse-Controller-Modbus-sensor-emulator).

Two jobs, both driven from the web UI:
1. **Scan** the RS-485 bus for responding Modbus slave addresses.
2. **Exercise** the wind speed/direction interface: read live registers, decode
   them, and write the DUT's calibration/config holding registers.

## 2. The three repos in play

| Repo | Role | Hardware | Modbus role |
|---|---|---|---|
| `greenhouse-Controller-Modbus-sensor-emulator` | Template — donor of everything *non*-Modbus | M5Stack Atom Lite + Atomic RS485 Base | **Slave** (emulates FG6485A + S200) |
| `windmeters-modbus-interface` | Device under test (DUT) | Custom CH32V003 PCB | **Slave** (real wind sensor bridge, in development) |
| `windmeters-modbus-interface-tester` (this repo) | The tool this doc designs | M5Stack AtomS3 + Atomic RS485 Base | **Master** (scans + exercises the DUT) |

**Terminology note:** the ask describes this as swapping the template's
"Modbus client" for a "master" — to be precise, the template implements a
Modbus **slave** (it answers FC03/FC04 requests via `modbus_slave_task`). What
actually gets swapped is *slave → master*: this tester initiates requests
instead of answering them. Same physical layer (RS-485, half-duplex
transceiver, 9600 8N1), opposite conversational role.

## 3. Scope: preserved vs. replaced vs. new

**Preserved from the template as-is (non-Modbus infrastructure):**
- WiFi AP/STA manager + auto-switch, AP SSID pattern `<Name>-<last2MACbytes>`
- mDNS (`*.local`)
- NTP sync with manual fallback, POSIX TZ resolution from lat/lon
- Web server: HTTP + WebSocket, ~1 s live push
- **GUI assets ported as-is**: `firmware/data/index.html` + `style.css` +
  `app.js` — vanilla HTML/CSS/JS, no framework or CDN dependency (confirmed
  by reading the actual files, not just the repo tree). The template is one
  scrollable page with a `<section>` per feature, not separate routed
  pages — that structure carries over directly (§7), including `app.js`'s
  `post(url, body)` / `setText()` / `setBadge()` helpers and its
  `type`-keyed WebSocket message router.
- NVS persistence layer and pattern
- RGB LED status feedback (idle/activity/error colour convention) — ported
  directly; confirmed working on this hardware at GPIO35 (WS2812B, see §4.1)
- FreeRTOS task-per-concern architecture, mutex-guarded shared state
- PlatformIO project layout (`firmware/`, `webMock/`, `design/`, `documentation/`)
- `webMock/` desktop Python/Flask mock, repointed at master-side responses

**Replaced (this is the actual point of the fork):**
- `modbus_slave_task` → `modbus_master_task` (initiates, doesn't answer)
- FG6485A + S200 hard-coded register emulation → generic register read/write
  against whatever the DUT currently exposes
- Manual/Live/Replay/REST *sourcing* modes — these exist in the template only
  to feed synthetic values into the emulated slave. A master has nothing to
  synthesize (it reads a real physical sensor through a real DUT), so all four
  modes are dropped rather than ported.
- "Sensor Configuration" page → **Bus Scanner** + **Wind Speed** / **Wind
  Direction** pages (§7; shipped as one combined "Wind Test" page at
  first, split into two 2026-07-02)

**New** (as new `<section>` blocks inside the ported `index.html`, styled
with the existing `style.css` classes and wired up with the existing
`app.js` helpers and message router — not a rebuilt frontend):
- Bus scanner (sweep address range, report which respond and how)
- Register Explorer (manual single-shot FC03/04/06/16 tool, arbitrary address/type/scale)
- Wind Speed / Wind Direction panels (live decode of each type's own
  register map + config write-back)

## 4. Hardware

### 4.1 AtomS3 (M5Stack, ESP32-S3)

**Confirmed on the bench: RX = `G5`, TX = `G6`.** This supersedes the
official M5Stack pin-map page, which lists the `HY2.0-4P` Grove port as
`G1`/`G2`/`5V`/`GND` — whatever the Atomic RS485 Base actually connects
through on this hardware, it isn't that pair. Empirical result wins; see
§4.2 and §9.

Everything else on the module (`G7`/`G8` ADC/touch, `G38`/`G39` I²C, LCD/IMU/
button/IR pins) is unused by this design — internal to the AtomS3 or on solder
pads we're not using. Source: `documentation/AtomS3/C123_PinMap_01.jpg`
(the pin-map graphic that turned out not to match reality for the Grove pins).

Deltas from the Atom Lite the template was built for:
- **Native USB-CDC** (ESP32-S3) — flashing/monitor over the same USB-C port,
  no CP2104 USB-serial bridge. Shouldn't need driver changes, but board id
  and upload behaviour differ from `m5stack-atom` — verify `pio run -t upload`
  actually resets into bootloader the same way.
- **Onboard RGB LED confirmed at GPIO35** (WS2812B, single LED, GRB order,
  driven via FastLED) — verified with a working test sketch,
  `Atom-RS485-Base-explorations/blinkyS3/main.cpp`, on the actual board this
  design targets. That board has **no LCD** — the pin-map graphic's LCD
  block (`G15/G16/G17/G21/G33/G34`) referenced earlier belongs to a
  different, screen-equipped AtomS3 variant and doesn't apply here.
  (Corrects an earlier pass in this doc that assumed the LCD-equipped
  variant and moved status to the screen — reverted back to the template's
  original RGB LED convention, now with a confirmed pin. The test sketch's
  own comment calls this LED "AtomS3 Lite" — naming aside, it's confirmed
  present and working on the target hardware, which is what matters here.)
  **Decision: keep the template's RGB LED status convention as-is**, GPIO35
  instead of GPIO27, same idle/scanning/valid-frame/error colour states
  (§6.2, §8).

### 4.2 Atomic RS485 Base

Same base hardware as the template. From
`documentation/Atomic RS485 Base/` schematic:
- Transceiver: **SP3485EN** (half-duplex RS-485), 3.3 V logic.
- **Auto direction control in hardware** — `DE`/`RE̅` are tied together and
  driven by a transistor (Q1) sensing the UART TX idle level, biased by R3.
  No `DE`/`RE` GPIO needed from firmware. This is *simpler* than
  greenhouse-Controller's own `drivers/modBus/src/modbus_rtu.h`, which
  manually asserts/drops a `PIN_RS485_DE_RE` GPIO around each transaction
  (that one drives a Sipex SIT65HVD08P) — one less thing to get wrong here,
  but also means: if TX framing is wrong, bus turnaround misbehaves in a way
  that's harder to debug because there's no explicit DE signal to scope.
- 120 Ω termination resistor (R4) present but **unpopulated/NC by default** —
  matches the DUT board's own default-disconnected terminator jumper. Bridge
  both ends only if the tester sits at a physical end of the daisy chain.
- TVS protection on A/B/GND (D1–D3, SMAJ6.5CA-E3).
- Onboard buck (AOZ1282CI) can regulate 12 V → 5 V from an external 4-pin
  power+data header (`J1`) — **not used here**. Power the tester from USB-C
  only and tap just A/B/GND onto the bench bus. Reason: the DUT's own field
  wiring is spec'd for up to 24 V passive PoE-style injection (§5); the
  base's buck input is nominally a 12 V rail (that's the label on the
  schematic, `+12V`). The AOZ1282CI itself is rated for up to 36 V in, so
  24 V wouldn't damage the regulator — but the base was laid out and
  labelled for 12 V, so treat 24 V-in as an unvalidated deviation from its
  designed operating point, not a checked/blessed configuration.
- Physical: stacks on the Grove port, ~24×48 mm footprint, matches Atom form
  factor.

**Confirmed on the bench:** `G5` = RX, `G6` = TX — the base's own header
(silkscreened `3V3, RX, TX, x, x`) lands on those two AtomS3 pins. Init as
`Serial2.begin(9600, SERIAL_8N1, 5, 6)` (or equivalent `HardwareSerial`
call); no further polarity verification needed.

## 5. Device under test — current register map

**Authoritative source as of 2026-07-02: the DUT's own Technical Design
Specification**, `windmeters-modbus-interface/design/TDS.md` §2.7/§2.8
(v0.6) — a formal, requirement-numbered spec (FR-MB*/FR-S*) that superseded
this section's earlier, less certain register-map guesses. This section is
now a summary mirroring that TDS, kept in sync by hand
(`firmware/lib/wind_poll/wind_poll.h` is the tester's implementation of it)
— re-check against the TDS directly if the two ever disagree, don't trust
this copy blindly. The Register Explorer (§7) remains the tool that
survives the TDS changing again without a tester rebuild.

- MCU: CH32V003J4M6 (RISC-V), transceiver MAX3485, 24 V passive PoE-style
  power over RJ45 (pins 4/5 +, 7/8 −).
- Three build variants, distinguished by slave address (solder-jumper
  selectable, TDS FR-S03): wind **speed** (cup anemometer, pulse counting on
  a hardware timer) at address **30** (or 35 jumpered), wind **direction**
  (potentiometer, ADC) at address **31** (or 36 jumpered), and **combined**
  (both sensors, one board, one slave) at address **32** (or 37 jumpered) —
  added to the DUT 2026-07-11.
- 9600 8N1 (TDS FR-MB01). No baud config register — fixed, unlike the S200
  sensor's `0x1001` in the existing greenhouse firmware.

**One register layout across all builds, not a per-build map (2026-07-02
correction, TDS v0.6 FR-MB27; extended 2026-07-11 for the combined build):**
earlier revisions of this section assumed wind speed and wind direction
implement *different, type-specific* register blocks (first a single
5-input/4-holding map shared by both, then — once the physical-separation
finding landed — two separate, differently-sized maps). The DUT's own TDS
settled this differently once it matured enough to specify it formally:
**every build implements the same register layout at the same addresses,**
as far as it goes — a register the active build's sensor doesn't have
simply reads 0 rather than not existing. The one genuine exception: the
**combined** build's input map is one register longer than the
single-sensor builds' (13 vs 12) — it adds `dir_raw_adc` at raw `0x000C`
(30013), since raw `0x0004` on that build carries the speed pulse count
instead of the direction raw ADC the single-sensor builds put there. A
single-sensor build's own map edge stays at `0x000C` (12 registers) — FC04
past that returns exception 02 (illegal data address, FR-MB13), so the
tester requests 12 or 13 registers depending on which build it's polling,
not a hardcoded constant. There is also **no device-address holding
register** as of TDS v0.6 (FR-MB07/FR-MB26) — the Modbus address is
hardware-configured only (build define + PC4 solder jumper) and cannot be
read or changed over Modbus; a write to the register address that concept
used to occupy now correctly returns exception 02 (illegal data address).

### Input registers (FC04, read-only) — TDS §2.7

●&nbsp;=&nbsp;carries real data on that build. ○&nbsp;=&nbsp;reads 0 on that build (FR-MB27). —&nbsp;=&nbsp;not mapped on that build (exception 02).

| Address (raw) | Modicon # | Content | Unit | Range | Speed | Direction | Combined |
|---|---|---|---|---|---|---|---|
| `0x0000` | 30001 | Wind direction, instantaneous | 0.1° | 0–3599; 65535 = sensor fault | ○ | ● | ● |
| `0x0001` | 30002 | Wind speed, instantaneous | 0.1 m/s | 0–65535 | ● | ○ | ● |
| `0x0002` | 30003 | Wind direction, averaged | 0.1° | 0–3599; 65535 = sensor fault | ○ | ● | ● |
| `0x0003` | 30004 | Wind speed, averaged | 0.1 m/s | 0–65535 | ● | ○ | ● |
| `0x0004` | 30005 | Raw diagnostic | build-specific | speed/combined: pulse count last window; direction-only: last raw 10-bit ADC (0–1023) | ● | ● | ● |
| `0x0005` | 30006 | Status flags (bitfield) | — | bit0=no window yet, bit1=averaging not filled, bit2=direction fault | ● | ● | ● |
| `0x0006` | 30007 | Identification | — | high byte = build type (0x01/0x02/0x03), low byte = fw version | ● | ● | ● |
| `0x0007` | 30008 | Uptime since reset | s | 0–65535, saturating | ● | ● | ● |
| `0x0008` | 30009 | Bus CRC error count | — | 0–65535, wrapping | ● | ● | ● |
| `0x0009` | 30010 | Served request count | — | 0–65535, wrapping | ● | ● | ● |
| `0x000A` | 30011 | Seconds since last pulse | s | 0–65535, clamped | ● | ○ | ● |
| `0x000B` | 30012 | Gust: max window speed in current averaging window | 0.1 m/s | 0–65535 | ● | ○ | ● |
| `0x000C` | 30013 | Wind direction, raw 10-bit ADC — **combined build only** | — | 0–1023 | — | — | ● |

The tester decodes and displays the measurement-relevant fields (direction/
speed instant+average, raw diagnostic, gust, seconds-since-pulse, and on the
combined build, `dir_raw_adc`) on their respective tabs; the device-level
diagnostics (status/identification/uptime/CRC-count/request-count) aren't
decoded into the GUI — they're real, documented registers reachable via
Register Explorer, just not wind measurement data.

### Holding registers (FC03 read / FC06 write single / FC16 write multiple) — TDS §2.8, identical on every build

All six registers **persist across reset and power-loss in the DUT's
on-chip non-volatile storage (TDS FR-S39)** — the Default column below is
only what a *blank or corrupt* store falls back to (TDS FR-S21), not what
every reset resets to; a value a master writes stays written. Every build
accepts writes to all 6 (FR-MB27) even though only some are functionally
meaningful per build — 40005/40006 (added 2026-07-11) affect only the speed
path and are inert, but still present/writable/persisted, on a
direction-only build (TDS FR-S40).

| Address (raw) | Modicon # | Purpose | Unit | Valid range | Default |
|---|---|---|---|---|---|
| `0x0000` | 40001 | Wind direction calibration offset | 0.1° | 0–3599 | 0 |
| `0x0001` | 40002 | Measurement window duration | ms | 100–60000 | 1000 |
| `0x0002` | 40003 | Averaging window | s | 1–600 (also constrained relative to the measurement window, TDS FR-S31) | 10 |
| `0x0003` | 40004 | Low-speed cut-off threshold | 0.1 m/s | 0–50 | 4 |
| `0x0004` | 40005 | Anemometer calibration factor C | 0.001 m/rotation | 1–6553 | 980 |
| `0x0005` | 40006 | Anemometer pulses per rotation | pulses/rot | 1–1000 | 1 |

The tester's Wind Speed tab exposes measurement window, averaging window,
and low-speed cutoff; Wind Direction exposes direction offset and averaging
window; Wind Combined exposes all six — matching which fields are
functionally relevant per build, even though the wire-level map no longer
restricts which registers a given build's writes will accept.

> ⚠️ **Scaling gotcha:** this DUT encodes at **×10** (implied one decimal
> place). The S200 sensor already integrated into greenhouse-Controller's
> firmware encodes wind at **×1000** (see `drivers/s200/src/s200.h`). Don't
> reuse S200 decode constants here — different device, different scale
> factor. The Register Explorer's scale field should default to something
> the user sets explicitly per-register, not a hardcoded ÷1000.

> ⚠️ **Addressing convention — raw is canonical here.** The DUT's own docs
> use 30001/40001-style ("Modicon") numbers, but this project's existing
> drivers (`s200.h`, `modbus_rtu.h`) already address registers as plain
> 0-based wire addresses and use no Modicon numbers anywhere. To keep one
> convention across the codebase rather than adding a second dialect, this
> doc and the tester's internal API treat the **raw address as canonical**
> (the tables above lead with it); Modicon numbers appear only as a
> cross-reference column and as an accepted *input* format in the Register
> Explorer (§7) — converted at the UI boundary via `wire_address =
> (modicon_number % 10000) − 1` and never carried further into the code.
> Worth raising the same convention switch upstream in
> `windmeters-modbus-interface`'s own `scratchBook.md`, since that repo is
> the actual source of the inconsistency (§9).

## 6. Software architecture

### 6.1 Build

Mirrors the template's `platformio.ini` almost exactly (confirmed from the
template repo: `platform=espressif32@6.3.2`, `board=m5stack-atom`,
`framework=arduino`, `fastled/FastLED`, `spiffs`, upload 1.5 Mbaud / monitor
115200):

```ini
[env]
platform = espressif32@6.3.2
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
build_flags = -DCORE_DEBUG_LEVEL=5

[env:windmeterTester]
board = m5stack-atoms3      ; confirmed — official PlatformIO espressif32 board id
board_build.filesystem = spiffs
lib_deps =
    fastled/FastLED           ; drives the onboard WS2812 RGB LED at GPIO35 (§4.1) — confirmed via blinkyS3 test sketch
```

### 6.2 Tasks

| Task | Priority | Responsibility |
|---|---|---|
| `modbus_master_task` | Highest | Sole owner of the RS-485 UART. Executes one queued transaction (probe / read / write) at a time, enforces timeout + inter-frame gap, posts every TX/RX frame to the log queue. |
| `scan_task` | Normal | On request, sweeps an address range through `modbus_master_task`, streams progress + hits to the web UI. Suspended when idle. |
| `wind_poll_task` | Normal | While one of the Wind Speed / Wind Direction / Wind Combined tabs targets an address (one at a time, §9), polls that build's input block (§5 — 12 registers on the single-sensor builds, 13 on combined) at a configurable interval via `modbus_master_task`, decoding only the fields meaningful for the active type; updates shared state under mutex. Suspended when idle. |
| `web_server_task` | Normal | HTTP + WebSocket server. Unchanged from template. |
| `wifi_manager_task` | Normal | AP/STA switching, mDNS. Unchanged from template. |
| `ntp_task` | Normal | Time sync, used only to timestamp the traffic log. Unchanged from template. |
| `led_status_task` | Low | RGB LED feedback on GPIO35 (WS2812B) — idle / scanning / valid-frame / error colour states, same convention as the template (GPIO27 on Atom Lite). |

Same rule as the template's `modbus_slave_task`: exactly one task ever
touches the UART. `scan_task` and `wind_poll_task` submit work to
`modbus_master_task` (queue) rather than opening the port themselves.

### 6.3 Modbus master core — API shape

Not a code fork of greenhouse-Controller's `drivers/modBus` (different
framework — that's ESP-IDF, this is Arduino; and no DE/RE pin needed here per
§4.2) — but worth deliberately mirroring its shape, since it's an
already-proven pattern in this exact sensor family:

```c
typedef enum {
    MB_OK = 0,
    MB_ERR_TIMEOUT,
    MB_ERR_CRC,
    MB_ERR_EXCEPTION,   // decode the exception code (illegal fn/addr/value) for the UI
    MB_ERR_FRAMING,
} mb_status_t;

mb_status_t mb_read_holding_registers(uint8_t addr, uint16_t start, uint8_t count, uint16_t *out);
mb_status_t mb_read_input_registers  (uint8_t addr, uint16_t start, uint8_t count, uint16_t *out);
mb_status_t mb_write_single_register (uint8_t addr, uint16_t reg, uint16_t value);          // FC06 — DUT config writes
mb_status_t mb_write_multiple_registers(uint8_t addr, uint16_t start, uint8_t count, const uint16_t *values);
```

Defaults: 9600 8N1, 200 ms timeout, 1 retry — the same numbers
greenhouse-Controller already runs successfully against the S200 on the same
physical-layer family. Both timeout and retry count should be
runtime-configurable from the web UI (§7) anyway, since a bring-up board is
exactly where the defaults are most likely to be wrong.

## 7. Web UI

**The GUI is the template's, ported — not redesigned.** `firmware/data/`
is vanilla HTML/CSS/JS, no framework, no build step: one scrollable
`index.html` with a `<section>` per feature, styled by `style.css`, wired
up by `app.js`. What this doc calls "pages" below are sections on that one
page, not separate routes or files. `app.js` already provides a
`post(url, body)` helper for form submissions, `setText()` / `setBadge()` /
`setSliderInput()` DOM helpers, and a WebSocket message router keyed on a
`type` field — new sections use the same helpers and the same router
convention (see WebSocket push below) rather than a parallel scheme.

Sections, replacing the template's sensor-config section with scan/test/explore:

- **Status (Home)** — WiFi mode/SSID/IP/RSSI, NTP sync indicator, uptime.
  Unchanged concept from template. Pinned to the top of the page; doesn't
  move into the tab layout below (2026-07-02 GUI restructure).
- **Bus Scanner** *(new)* — address range inputs (default 1–247), start/stop,
  live progress bar over WebSocket, results table: address, responded Y/N,
  function code(s) that worked, round-trip ms, raw probe response hex.
  Originally shipped with quick presets for 1/30/31/35/36/44 — removed
  2026-07-02 (S200/address 44 is permanently out of scope, per the §9
  decision below, and the remaining windmeter presets weren't pulling their
  weight once Wind Speed/Direction got their own tabs with the right
  default address already filled in).
- **Wind Speed** and **Wind Direction** *(new — the core ask, split
  2026-07-02)* — one tab each, not a combined "Wind Test" panel: speed and
  direction are physically separate DUT units at separate addresses (§5),
  so a single panel covering both no longer matched the hardware. Each tab
  has its own register-map reference table (§5), slave-address + poll-interval
  controls, live decoded values, and a config form scoped to the holding
  registers functionally relevant to that type (Wind Speed: measurement
  window/averaging window/low-speed cutoff; Wind Direction: direction
  offset/averaging window — no device-address field on either as of TDS
  v0.6, §5) with FC06 write-back.
- **Wind Combined** *(new 2026-07-11 — a third DUT build variant, not a
  GUI-only feature)*: the DUT gained a build that serves both sensors from
  one board behind one slave address (§5), so it gets its own third tab
  rather than being folded into Speed/Direction. Same shape as the other
  two tabs — register-map table, controls, config form with FC06 write-back
  — except its config form shows all six holding registers (the two new
  ones, calibration_c/pulses_per_rotation, only make sense on this tab and
  Wind Speed) and its live-value area shows both a Speed group and a
  Direction group side by side, since one poll of this build's 13-register
  input block carries both sensors' data in one atomic snapshot.
- Only one of the three wind tabs can poll at a time in v1 — same "one
  target at a time" constraint as the original combined panel this whole
  section replaced, just now expressed as "one *tab*, not one *field of a
  shared panel*" (§9). Starting one tab's poll silently stops whichever of
  the other two was running; the GUI corrects the other tabs' Start/Stop
  buttons on the next status update rather than leaving them showing a
  stale "running" state.
- **Register Explorer** *(new — generic tool)* — manual single-shot request:
  address, FC (03/04/06/16), start register entered as a **raw 0-based
  address by default** (canonical, §5), with an optional Modicon 5-digit
  entry mode ("40003") that converts to raw at input time and is never
  stored or displayed as the canonical value, plus count/value(s). Shipped
  simpler than originally sketched here: no uint16/int16/uint32/int32 or
  byte/word-order interpretation modes, just raw register values — every
  register on the DUT so far is a plain unsigned 16-bit value (§5), so
  that layer wasn't needed and wasn't built (avoids the "don't design for
  hypothetical future requirements" trap). Decoded result + raw hex shown
  side by side. This is the tool that survives DUT register map changes
  without a firmware rebuild on the tester side. Saved/favourite queries
  are backlog, not v1 (§9) — every query is typed fresh for now.
- **Modbus Log** — same concept as the template (scrolling TX/RX frames, hex
  + decoded, clear button), just both directions now originate from us.
  Pinned to the bottom of the page, doesn't move into the tab layout
  (2026-07-02 GUI restructure, same reasoning as Status above). GUI
  timestamps are `HH:MM:SS` (wall-clock local time once NTP is synced,
  uptime-based otherwise) and the table caps at the last 50 entries —
  raised from an original 30/32 once real bench use showed that was too
  short to keep a whole scan or a short multi-query session in view at
  once.
- **System Settings** — WiFi (SSID/password), NTP server, manual time, and
  Modbus timeout/retries, one tab. Grew from the template's WiFi-only
  Settings section once Modbus timeout/retries needed a home too; the
  Modbus fields pre-populate from the live status stream (NVS-backed)
  instead of loading empty, so a bench user can see the current values
  before deciding whether to change them.

### WebSocket push — `type`-routed, matching `app.js`'s existing message router

`app.js` already dispatches incoming JSON by a `type` field (`status`,
`log`, `log_clear`, `replay_window` today, per the template's own message
router). Reusing that router — instead of switching to one large per-tick
object — means new messages are just new `type` values the existing
dispatch `switch` grows a case for. `replay_window` is dropped (Replay mode
is gone, §3); `status`, `log`, and `log_clear` carry over unchanged; `scan`,
`wind`, and `interface` are new:

```json
// type: "status" — ~1 s cadence, same as template. fw_version and
// mb_timeout_ms/mb_retries added 2026-07-02 (System Settings pre-fill,
// footer version display) — this example had drifted out of sync with
// the real payload before that; keep it matched going forward.
{ "type": "status",
  "fw_version": "0.4.0",
  "wifi_mode": "STA", "wifi_ssid": "...", "wifi_ip": "...",
  "ntp_synced": true, "local_time": "2026-07-01T14:30:45+02:00",
  "uptime_s": 4021, "mb_timeout_ms": 200, "mb_retries": 1,
  "bus": { "crc_errors": 0, "timeouts": 2, "last_exception": null } }

// type: "scan" — pushed while a Bus Scanner sweep is running
{ "type": "scan", "current_addr": 37, "range_end": 247, "found": [1, 31, 44] }

// type: "wind" — pushed while one of Wind Speed / Wind Direction / Wind
// Combined is polling (only one at a time, §9); "sensor_type" tags which
// tab it's for. speed/direction each carry only that type's own fields
// (2026-07-02 split — this used to be one shape carrying both sensors'
// fields at once). gust_ms/seconds_since_pulse and dir_fault/raw_adc
// added 2026-07-02 once the DUT's TDS specified those registers (§5).
// "combined" (added 2026-07-11) is the union of both shapes below, same
// field names — it's not the same thing as the pre-split shared shape:
// this is one physical DUT whose own register map genuinely carries both
// sensors' data, not two devices' data merged for convenience.
{ "type": "wind", "sensor_type": "speed", "has_data": true,
  "speed_instant_ms": 4.2, "speed_avg_ms": 3.9, "raw_pulses": 27,
  "gust_ms": 6.5, "seconds_since_pulse": 8, "age_ms": 420 }
{ "type": "wind", "sensor_type": "direction", "has_data": true,
  "dir_instant_deg": 183.4, "dir_avg_deg": 181.0, "dir_fault": false,
  "raw_adc": 512, "age_ms": 420 }
{ "type": "wind", "sensor_type": "combined", "has_data": true,
  "speed_instant_ms": 4.2, "speed_avg_ms": 3.9, "raw_pulses": 30,
  "gust_ms": 6.5, "seconds_since_pulse": 8,
  "dir_instant_deg": 182.8, "dir_avg_deg": 181.0, "dir_fault": false,
  "raw_adc": 520, "age_ms": 420 }

// type: "interface" — pushed opportunistically whenever Wind Speed /
// Direction / Combined is actively polling AND has produced at least one
// successful reading (added with the Wind Interface tab, §7's tab list):
// the DUT's device/system diagnostic registers (TDS §2.7, raw
// 0x0005-0x0009) ride along in whichever sensor type's own FC04 block is
// currently polling, so there's no separate poll loop of its own. One
// shape regardless of which of the three is active (FR-MB27) — unlike
// "wind" above, no "sensor_type" tag. Same fields the single-shot
// POST /wind/interface/read returns (that route splices "ok":true onto
// this same builder's output instead of "type").
{ "type": "interface", "addr": 30, "build_type": 1, "build_name": "wind_speed",
  "fw_version": 3, "status_flags": 0, "status_measurement_incomplete": false,
  "status_avg_not_filled": false, "status_dir_fault": false, "uptime_s": 4021,
  "crc_error_count": 0, "served_request_count": 812, "has_data": true,
  "age_ms": 340 }

// type: "log" / "log_clear" — unchanged from template
```

### NVS keys

| Key | Type | Default | Purpose |
|---|---|---|---|
| `wifi_ssid` / `wifi_pass` | str | "" | unchanged from template |
| `ntp_server` | str | `pool.ntp.org` | unchanged from template |
| `mb_baud` | u32 | 9600 | Modbus baud (kept configurable — DUT firmware is still moving) |
| `mb_timeout_ms` | u16 | 200 | Response timeout |
| `mb_retries` | u8 | 1 | Retry count before marking a transaction failed |
| `scan_range_start` / `scan_range_end` | u8 | 1 / 247 | Last-used scan range |
| `wind_speed_addr` | u8 | 30 | Last-used Wind Speed target (was one shared `wind_test_addr` before the 2026-07-02 split) |
| `wind_dir_addr` | u8 | 31 | Last-used Wind Direction target |
| `wind_comb_addr` | u8 | 32 | Last-used Wind Combined target, added 2026-07-11 |
| `wind_poll_ms` | u32 | 1000 | Poll cadence, shared by all three wind tabs — no reason a bench tool needs a different cadence per sensor type. Shortened from `wind_poll_interval_ms` (21 chars, over the 15-char NVS key limit — `cfg_keys.h`) |
| `wind_iface_addr` | u8 | 30 | Last-used Wind Interface tab target, independent of the other three — defaults to Wind Speed's address since registers `0x0005`-`0x0009` read identically on every build (§5) |

## 8. Development sequence

Adapted from the template's 13-step sequence:

1. Board bring-up: UART loopback test on the confirmed `G5` (RX) / `G6` (TX) pins (§4.1)
2. Modbus master core: CRC16, frame builder, timeout/retry (§6.3)
3. **Bus Scanner** (replaces "modbus slave skeleton")
4. **Register Explorer** generic read/write (replaces FG6485A/S200 emulation steps)
5. **Wind Speed / Wind Direction** panels wired to the current §5 register
   maps (shipped as one combined "Wind Test" panel at first, split 2026-07-02)
6. NVS + settings persistence
7. WiFi manager (AP/STA, mDNS) — straight port
8. Web interface shell (HTTP, WebSocket, page routing) — straight port
9. NTP + manual time — straight port
10. RGB LED status (GPIO35) — straight port of the template's idle/scanning/valid-frame/error colour convention (§4.1)
11. Modbus traffic log (queue → WebSocket → UI) — straight port, roles reversed
12. Integration test against a real `windmeters-modbus-interface` board on the bench
13. Regression pass whenever the DUT's register map changes upstream

## 9. Open questions

- [x] AtomS3 → RS485 base RX/TX polarity — resolved by bench test:
      **`G5` = RX, `G6` = TX**. Contradicts the official M5Stack pin-map
      page (which lists the `HY2.0-4P` Grove port as `G1`/`G2`) — the
      confirmed empirical result is what's used throughout this doc
      (§4.1, §4.2, §8).
- [x] AtomS3 RGB LED GPIO — resolved: **GPIO35**, WS2812B, confirmed via a
      working test sketch (`Atom-RS485-Base-explorations/blinkyS3`) on the
      actual target hardware. Template's RGB LED convention ports directly
      (§4.1, §6.2, §8) — no LCD involved.
- [x] PlatformIO board id — confirmed `m5stack-atoms3` against PlatformIO's
      espressif32 board registry
- [x] Multi-device scanning — **decision: deferred to v2.** Still true after
      the wind-speed/direction split below: each tab targets one address at
      a time, and only one tab polls at all at any given moment (not two
      simultaneously) — splitting into two tabs is not the same thing as
      the deferred multi-device dashboard, which would mean several devices
      live on screen at once. Revisit a real dashboard once the
      single-device flow is proven on the bench.
- [x] Wind speed/direction as one combined panel vs. two — **decision
      (2026-07-02): split into two.** Originally modelled as one Wind Test
      panel reading a single device's combined 5-input/4-holding register
      block (§5's old table). Reading the DUT's own
      `windmeters-modbus-interface/design/scratchBook.md` more closely
      showed speed and direction are separate physical units sharing one
      PCB/firmware source, distinguished at build time and by slave address
      (solder-jumper selectable) — not one device exposing both sensors'
      registers together. Corrected §5's register tables (now two maps, not
      one) and §7's GUI description (two tabs, not one panel) to match.
      `wind_poll`'s core, its FreeRTOS task, and the `/wind/*` endpoints are
      all now parameterised on a `wind_sensor_type_t`, each type only ever
      touching the registers its own firmware implements.
- [x] Register Explorer "favourites" — **decision: backlog**, not v1.
- [x] DUT scratchBook TODOs (e.g. low-speed cutoff register 40005) —
      **decision: reference only.** This doc links to
      `windmeters-modbus-interface`'s own scratchBook.md rather than
      mirroring its TODO list; that repo stays the source of truth for
      register-map changes.
- [x] Addressing convention — **decision: raw 0-based wire addresses are
      canonical**, matching the existing `s200.h`/`modbus_rtu.h` drivers
      (neither uses Modicon numbers). Modicon 30001/40001-style numbers are
      accepted as Register Explorer input only, converted at the boundary,
      never carried further into the tester's code (§5, §7). **Applied
      upstream too:** `windmeters-modbus-interface`'s own `design/
      scratchBook.md` has been updated to the same raw-address-canonical /
      Modicon-cross-reference convention (its `main.c` was still scaffolding
      with no register code yet, so this was a docs-only change there).

## 10. References

- Template: https://github.com/pe1mew/greenhouse-Controller-Modbus-sensor-emulator
- DUT / interface under test: https://github.com/pe1mew/windmeters-modbus-interface
- Local hardware refs: `documentation/AtomS3/`, `documentation/Atomic RS485 Base/`
- Confirmed-working RGB LED example: `Atom-RS485-Base-explorations/blinkyS3/main.cpp` (GPIO35, WS2812B, FastLED)
- Prior-art Modbus master (different framework, same physical-layer family):
  `greenhouse-Controller/drivers/modBus/src/modbus_rtu.h`,
  `greenhouse-Controller/drivers/s200/src/s200.h`
