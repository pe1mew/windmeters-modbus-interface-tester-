# Scratchbook — Windmeters Modbus Interface Tester

Status: **draft / working notes** — 2026-07-01

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
- "Sensor Configuration" page → **Bus Scanner** + **Wind Test** pages (§7)

**New:**
- Bus scanner (sweep address range, report which respond and how)
- Register Explorer (manual single-shot FC03/04/06/16 tool, arbitrary address/type/scale)
- Wind Test panel (live decode of the current known wind register map + config write-back)

## 4. Hardware

### 4.1 AtomS3 (M5Stack, ESP32-S3)

Grove port (`HY2.0-4P`, the only externally-wired connector once the RS485
Base is stacked underneath): `GND`, `5V`, `G2`, `G1`.

Everything else on the module (`G5–G8` ADC/touch, `G38`/`G39` I²C, LCD/IMU/
button/IR pins) is unused by this design — internal to the AtomS3 or on solder
pads we're not using. Source: `documentation/AtomS3/C123_PinMap_01.jpg`.

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

**Open hardware question:** which of `G1`/`G2` is RX vs. TX once stacked —
the base's own header is silkscreened `3V3, RX, TX, x, x` but that doesn't
pin down which AtomS3 Grove pin lands on which base signal. Confirm on the
bench (loopback test or scope) before hardcoding a `pin_config.h`; ESP32-S3's
GPIO matrix means either assignment is just a `Serial.begin(9600, SERIAL_8N1,
rxPin, txPin)` swap if guessed wrong on the first try.

## 5. Device under test — current register map

**This will move.** `windmeters-modbus-interface` is 4 commits in, with its
own `design/scratchBook.md` carrying open TODOs (e.g. a not-yet-implemented
low-speed cutoff register). Treat everything below as the *current* snapshot,
not a contract — this is exactly why the Register Explorer (§7) is the
primary tool here, not a nice-to-have wrapped around a fixed decoder.

- MCU: CH32V003J4M6 (RISC-V), transceiver MAX3485, 24 V passive PoE-style
  power over RJ45 (pins 4/5 +, 7/8 −).
- Two build variants sharing one register map shape, distinguished by slave
  address (solder-jumper selectable): wind **speed** (cup anemometer, pulse
  counting on a hardware timer) at address **30** (or 35 jumpered), wind
  **direction** (potentiometer, ADC) at address **31** (or 36 jumpered).
- 9600 8N1 (same as the template/base — no baud config register exists on the
  DUT yet, unlike the S200 sensor's `0x1001` in the existing greenhouse
  firmware).

### Input registers — FC04, read-only, Modicon 5-digit numbering

| Register | Content | Unit | Range |
|---|---|---|---|
| 30001 | Wind direction, instantaneous | 0.1° | 0–3599 |
| 30002 | Wind speed, instantaneous | 0.1 m/s | 0–65535 |
| 30003 | Wind direction, averaged | 0.1° | 0–3599 |
| 30004 | Wind speed, averaged | 0.1 m/s | 0–65535 |
| 30005 | Raw pulse count | pulses/interval | 0–65535 |

### Holding registers — FC03 read / FC06 write single / FC16 write multiple

| Register | Purpose | Unit | Range |
|---|---|---|---|
| 40001 | Device (slave) address | — | 1–247 |
| 40002 | Direction calibration offset | 0.1° | 0–3599 |
| 40003 | Measurement window | ms | default 1000 |
| 40004 | Averaging window | s | default 10 |
| ~~40005~~ | Low-speed cutoff (planned, not yet in firmware per DUT's own TODO) | — | — |

> ⚠️ **Scaling gotcha:** this DUT encodes at **×10** (implied one decimal
> place). The S200 sensor already integrated into greenhouse-Controller's
> firmware encodes wind at **×1000** (see `drivers/s200/src/s200.h`). Don't
> reuse S200 decode constants here — different device, different scale
> factor. The Register Explorer's scale field should default to something
> the user sets explicitly per-register, not a hardcoded ÷1000.

> ⚠️ **Addressing gotcha:** 30001/40001-style numbers are the human
> ("Modicon") convention, not the wire address. Wire address = `(register
> _number % 10000) − 1`. So `30001` → FC04 address `0x0000`; `40003` →
> FC03/06 address `0x0002`. The Register Explorer needs a toggle (§7) so this
> conversion happens in one place instead of being re-derived by hand each
> time.

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
| `wind_poll_task` | Normal | While the Wind Test panel targets an address, polls the input registers (§5) at a configurable interval via `modbus_master_task`; updates shared state under mutex. Suspended when idle. |
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

Pages, replacing the template's sensor-config page with scan/test/explore:

- **Status (Home)** — WiFi mode/SSID/IP/RSSI, NTP sync indicator, uptime.
  Unchanged concept from template.
- **Bus Scanner** *(new)* — address range inputs (default 1–247, with quick
  presets for 1/30/31/35/36/44 — the known defaults across this sensor
  family), start/stop, live progress bar over WebSocket, results table:
  address, responded Y/N, function code(s) that worked, round-trip ms, raw
  probe response hex.
- **Wind Test** *(new — the core ask)* — pick an address (from scan results
  or typed in), live view of registers 30001–30005 decoded per §5, compass
  dial for direction + numeric for speed, poll start/stop + interval, and a
  config form for 40001–40004 (address/offset/windows) with FC06 write-back.
  Targets one slave address at a time in v1 — a side-by-side multi-device
  dashboard is deferred to v2 (§9). Rough layout:

  ```
  ┌─ Wind Test ───────────────────────────────────┐
  │ Slave addr: [ 31 ▾]     Poll: [1000] ms  ▶/■ │
  │                                               │
  │      dir avg 181.0°        speed avg 3.9 m/s  │
  │        (compass dial)         (numeric)       │
  │  dir instant 183.4°     speed instant 4.2 m/s │
  │  raw pulses: 27           last poll: 420ms ago│
  │                                               │
  │ ── Config (holding regs) ──────────────────   │
  │ Device addr [31]  Dir offset [0.0°]           │
  │ Meas window [1000ms]  Avg window [10s]        │
  │                                    [Write]    │
  └───────────────────────────────────────────────┘
  ```
- **Register Explorer** *(new — generic tool)* — manual single-shot request:
  address, FC (03/04/06/16), start register (raw *or* Modicon 5-digit, with
  the §5 conversion applied automatically), count/value(s), interpretation
  (uint16/int16/uint32/int32, byte/word order, scale factor). Decoded result
  + raw hex shown side by side. This is the tool that survives DUT register
  map changes without a firmware rebuild on the tester side. Saved/favourite
  queries are backlog, not v1 (§9) — every query is typed fresh for now.
- **Modbus Log** — same concept as the template (scrolling TX/RX frames, hex
  + decoded, timestamp, clear button), just both directions now originate
  from us.
- **WiFi Settings** — unchanged from template.

### WebSocket push (~1 s, same cadence as template)

```json
{
  "mode": "idle | scanning | wind_test",
  "scan": { "current_addr": 37, "range_end": 247, "found": [1, 31, 44] },
  "wind": {
    "addr": 31, "dir_instant_deg": 183.4, "dir_avg_deg": 181.0,
    "speed_instant_ms": 4.2, "speed_avg_ms": 3.9, "raw_pulses": 27,
    "last_ok": true, "age_ms": 420
  },
  "bus": { "crc_errors": 0, "timeouts": 2, "last_exception": null },
  "wifi_mode": "STA", "wifi_ssid": "...", "wifi_ip": "...",
  "ntp_synced": true, "local_time": "2026-07-01T14:30:45+02:00"
}
```

### NVS keys

| Key | Type | Default | Purpose |
|---|---|---|---|
| `wifi_ssid` / `wifi_pass` | str | "" | unchanged from template |
| `ntp_server` | str | `pool.ntp.org` | unchanged from template |
| `tz_posix` | str | "" | unchanged from template |
| `mb_baud` | u32 | 9600 | Modbus baud (kept configurable — DUT firmware is still moving) |
| `mb_timeout_ms` | u16 | 200 | Response timeout |
| `mb_retries` | u8 | 1 | Retry count before marking a transaction failed |
| `scan_range_start` / `scan_range_end` | u8 | 1 / 247 | Last-used scan range |
| `wind_test_addr` | u8 | 31 | Last-used Wind Test target |
| `wind_poll_interval_ms` | u32 | 1000 | Wind Test poll cadence |

## 8. Development sequence

Adapted from the template's 13-step sequence:

1. Board bring-up: confirm `G1`/`G2` RX/TX polarity (§4.1/§4.2 open question), UART loopback
2. Modbus master core: CRC16, frame builder, timeout/retry (§6.3)
3. **Bus Scanner** (replaces "modbus slave skeleton")
4. **Register Explorer** generic read/write (replaces FG6485A/S200 emulation steps)
5. **Wind Test** panel wired to the current §5 register map
6. NVS + settings persistence
7. WiFi manager (AP/STA, mDNS) — straight port
8. Web interface shell (HTTP, WebSocket, page routing) — straight port
9. NTP + manual time — straight port
10. RGB LED status (GPIO35) — straight port of the template's idle/scanning/valid-frame/error colour convention (§4.1)
11. Modbus traffic log (queue → WebSocket → UI) — straight port, roles reversed
12. Integration test against a real `windmeters-modbus-interface` board on the bench
13. Regression pass whenever the DUT's register map changes upstream

## 9. Open questions

- [ ] AtomS3 `G1`/`G2` → RS485 base RX/TX polarity — still needs a bench
      check (loopback test or scope); couldn't find an official example
      sketch for plain AtomS3 + this specific base
- [x] AtomS3 RGB LED GPIO — resolved: **GPIO35**, WS2812B, confirmed via a
      working test sketch (`Atom-RS485-Base-explorations/blinkyS3`) on the
      actual target hardware. Template's RGB LED convention ports directly
      (§4.1, §6.2, §8) — no LCD involved.
- [x] PlatformIO board id — confirmed `m5stack-atoms3` against PlatformIO's
      espressif32 board registry
- [x] Multi-device scanning — **decision: deferred to v2.** v1 Wind Test
      targets one address at a time; revisit a side-by-side dashboard once
      the single-device flow is proven on the bench.
- [x] Register Explorer "favourites" — **decision: backlog**, not v1.
- [x] DUT scratchBook TODOs (e.g. low-speed cutoff register 40005) —
      **decision: reference only.** This doc links to
      `windmeters-modbus-interface`'s own scratchBook.md rather than
      mirroring its TODO list; that repo stays the source of truth for
      register-map changes.

## 10. References

- Template: https://github.com/pe1mew/greenhouse-Controller-Modbus-sensor-emulator
- DUT / interface under test: https://github.com/pe1mew/windmeters-modbus-interface
- Local hardware refs: `documentation/AtomS3/`, `documentation/Atomic RS485 Base/`
- Confirmed-working RGB LED example: `Atom-RS485-Base-explorations/blinkyS3/main.cpp` (GPIO35, WS2812B, FastLED)
- Prior-art Modbus master (different framework, same physical-layer family):
  `greenhouse-Controller/drivers/modBus/src/modbus_rtu.h`,
  `greenhouse-Controller/drivers/s200/src/s200.h`
