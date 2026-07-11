# What's Next — Windmeters Modbus Interface Tester

| Field        | Value                                    |
|--------------|-------------------------------------------|
| Document     | Next Phases                               |
| Project      | Windmeters Modbus Interface Tester        |
| Date         | 2026-07-11 (updated — combined build added, Phase 4's DUT-scaffolding blocker cleared) |
| Status       | Phase 2.5 done; Phase 3 eight-of-nine done (INT-01/02/03/04/06/07/08/09 — §3.2); only INT-05 remains — no longer blocked on the DUT firmware existing (the DUT's own HIL suite now covers all three builds), but this project's own INT-05/INT-06 runs against a physical unit are still outstanding (Phase 4). Wind speed/direction split + GUI restructure done 2026-07-02, after Phase 3's testing (§2.5); Wind Combined tab + third build variant added 2026-07-11 |
| Related docs | `design/progress.md` (what's done so far), `design/api.md` (Phase 2.5's spec), `design/completeRealisationPlan.md` §4 (Part C — Integration Test, the source table Phase 3 below reproduces), `design/realisationPlan.md` §4 (bring-up sequence, steps 3–5), `memory/gotcha-log.md` |

---

## 1. Overview

Phases 1 (Libraries), 2 (FreeRTOS Tasks + hardware-plumbing verification),
and 2.5 (Machine API) are complete — see `progress.md`. A real bus peer
(an FG6485A at address 1) got connected mid-session, so Phase 3 is now
partially, opportunistically underway rather than a clean future step.
Every remaining phase is one of:

- **Phase 3 — Integration Test.** Prove the tester actually talks Modbus to
  a real bus peer, not just that its own queues/tasks/UART/HTTP plumbing
  works in isolation. This is `completeRealisationPlan.md` Part C
  (INT-01…INT-09). **Eight of nine done** — INT-01/02 (cold boot + AP-mode
  WiFi config), INT-03 (scan), INT-04 (register read), INT-06 (write +
  read-back), INT-07 (log ordering), INT-08 (LED states, visually confirmed
  by the user), INT-09 (settings survive power-cycle) — see §3.2's table
  for detail, including three real bugs found and fixed along the way:
  INT-02 caught WiFi settings silently never taking effect (no reboot was
  ever triggered after saving), INT-06 hit a real sensor silently
  declining an out-of-range write (not a tester bug), and INT-09 caught a
  genuine NVS key-length bug. Only INT-05 remains, blocked on the DUT's
  firmware (Phase 4) — Phase 3 is otherwise complete. **S200 (address 44)
  is explicitly out of scope — will never be connected (2026-07-02
  decision) — so INT-03/04 are considered fully done at FG6485A/addr-1
  coverage, not partially done pending an S200.**
- **Phase 4 — Real DUT verification.** Repeat the Wind-Test-specific parts
  of Phase 3 against the actual `windmeters-modbus-interface` board once its
  firmware implements a register map. Currently blocked — reconfirmed today:
  that repo's `git log` still shows `3427df2 First scaffolding` as HEAD,
  `software/firmware/src/main.c` unchanged.
- **Phase 5 — Refinements.** A short backlog of gaps that are known,
  documented, and intentionally deferred — not blockers, but worth listing
  so they don't get lost.

None of these are expected to need new library or task *code* beyond
Phase 2.5 itself by default — Phase 3 in particular is mostly about running
the tester against real bus peers and fixing whatever real Modbus traffic
reveals that mocked/native tests structurally couldn't catch (framing edge
cases, timing under real UART latency, real bus contention).

---

## 2. Phase 2.5 — Machine API (`design/api.md`) — DONE

Full spec: `design/api.md`. Full completion detail, including two
correctness fixes to *existing* code and one real bug in the scan poll
loop found via hardware testing: `progress.md` §7. Summary:

- All 7 `/api/v1/*` endpoints (`POST /modbus`, `GET /spec`, `GET /status`,
  `POST`/`GET /scan`, `GET /log`, `GET /wind`) implemented in
  `web_server_task` + `web_core` — no new library, same pattern as the
  existing UI endpoints (`api.md` §10, resolved during elaboration).
  Hardware-verified, including against a real FG6485A sensor connected at
  address 1.
- Designed specifically so an LLM (Claude) can drive the tester over plain
  HTTP during bench sessions — one self-contained request/response per
  Modbus transaction, no session state, errors that explain themselves
  (`api.md` §1.1). Confirmed working this way: read real humidity/
  temperature values, Modicon-address conversion, function aliases, and a
  full bus sweep all round-tripped correctly over `curl`/`Invoke-WebRequest`.
- This is now the primary tool for driving Phase 3 below — a scan, a read,
  a write are all one HTTP call instead of clicking through the web UI for
  each bench check.

---

## 2.5 Wind speed/direction split + GUI restructure (2026-07-02) — DONE

Done after Phase 3's testing (§3) wrapped up, prompted by reading the DUT's
own `windmeters-modbus-interface/design/scratchBook.md` more closely: wind
speed and wind direction are physically separate units sharing one PCB/
firmware source, not one device exposing both sensors' registers together.
Full detail: `scratchbook.md` §5/§9, `completeRealisationPlan.md` §2/§3
TASK-WIND, `api.md` §5.5.

- **Backend split:** `wind_poll`'s decode/config core, `wind_poll_task`, and
  the `/wind/*` + `/api/v1/wind` endpoints are all now parameterised on a
  `wind_sensor_type_t` (`speed`/`direction`), each type only ever touching
  the registers its own firmware implements. Separate NVS keys
  (`wind_speed_addr`/`wind_dir_addr`, replacing one shared `wind_test_addr`).
  Only one type polls at a time — same constraint as before, now per-type
  instead of per-field.
- **GUI restructure:** Status pinned top, Modbus Log pinned bottom (both
  always visible), Bus Scanner/Wind Speed/Wind Direction/Register Explorer/
  System Settings moved into a tab bar between them. Bus Scanner's presets
  row removed (S200/address 44 permanently out of scope, and the remaining
  windmeter presets stopped pulling their weight once each wind tab has its
  own correct default address). System Settings' Modbus Timeout/Retries now
  pre-populate from the live status stream (NVS-backed) instead of loading
  empty.
- **Modbus Log:** GUI timestamps changed from raw milliseconds to
  `HH:MM:SS`; display cap raised from 30 to 50 entries (ring buffer
  capacity likewise raised from 32 to 50, `MB_LOG_CAPACITY`).
- **Real bug found and fixed while restructuring the log, not part of the
  original ask:** the WebSocket broadcaster only ever looked at the single
  newest log entry once per second. `mb_master_process()` logs a TX entry
  immediately followed by an RX entry sharing the same timestamp — for a
  fast one-off transaction (the common case for a Register Explorer query)
  the TX almost always got silently skipped, since RX became "the newest
  entry" before the next broadcast tick ever looked. Not a data-loss bug —
  both entries were always correctly in the ring buffer, so `GET
  /api/v1/log` was never affected — only the live WebSocket tail the GUI
  actually watches. Fixed with a monotonic `mblog_total_appended()` counter
  so the broadcaster catches up on every entry since its last tick, not
  just the newest one. Verified on hardware: a single query now reliably
  shows both TX and RX (previously intermittent), and a 3-query rapid burst
  loses none of the 3 TX entries.
- **Incidental fix, found while updating this documentation, not part of
  the original ask:** `GET /api/v1/spec`'s `dut_register_snapshot` field
  (`web_server_task.cpp`'s hand-maintained `DUT_REGISTER_SNAPSHOT_JSON`
  constant) still described the old combined 5-input/4-holding map after
  the split landed — nothing regenerates that constant automatically, and
  it was missed at the time. An LLM or script bootstrapping a session from
  `/api/v1/spec` alone (`api.md` §1.1's whole reason for that endpoint
  existing) would have gotten a register layout that no longer exists.
  Fixed to the same `wind_speed`/`wind_direction`-keyed shape as the rest
  of the split (`api.md` §5.1).
- **142/142 native tests pass** (up from 91 at Phase 2.5; new coverage for
  the type-aware wind core, the log broadcaster's catch-up counter, and the
  enriched WS status payload). Hardware build, flash (firmware + filesystem),
  and live-device verification done for every piece above — see
  `progress.md` for the session's full verification log once it's updated
  to match.

---

## 3. Phase 3 — Integration Test

### 3.1 Bench setup required

**Actual setup (2026-07-02), superseding the original plan below:** a real
physical FG6485A sensor at address 1, not the software emulator. No S200 —
out of scope, will never be connected (2026-07-02 decision), so the
emulator's address-44 S200 role from the original plan is dropped
entirely, not just deferred.

Original reasoning, kept for context — `realisationPlan.md` §4: the real
DUT has no register firmware yet, so
`greenhouse-Controller-Modbus-sensor-emulator` (an existing AtomLite Modbus
RTU slave, present locally at `../greenhouse-Controller-Modbus-sensor-emulator`)
was meant to stand in wherever a test doesn't specifically need the DUT's
own register map. A real FG6485A filling that role directly is strictly
better evidence than the emulator would have been — this note stays mainly
so `../greenhouse-Controller-Modbus-sensor-emulator` isn't mistaken for
something still needed.

- The tester itself: AtomS3 + Atomic RS485 Base, already flashed and
  verified through Phase 2.
- The real FG6485A, responding at address 1.
- A shared RS-485 bus: the tester's G5/G6 leads connect to the sensor's
  bus terminals. **Not** the G5↔G6 loopback jumper used for the original
  UART self-test — that jumper must come off first, or it will intercept
  the tester's own TX as a malformed echo instead of letting it reach the
  emulator.
- A laptop/PC on the same network as the tester (STA mode) for the
  WebSocket/HTTP-driven tests (INT-01, 02, 07, 08, 09).

### 3.2 Test sequence

`completeRealisationPlan.md` §4's table, reproduced here with a suggested
run order — cheapest/most-isolated first, so a wiring mistake on the shared
bus doesn't block progress on the tests that don't need it yet:

| Order | Test ID | Scenario | Needs a bus peer? | Validation | Status |
|---|---|---|---|---|---|
| 1 | INT-01 | Cold boot, no NVS config | No | Device starts in AP mode; `index.html` loads with every section present | **Done** — WiFi credentials cleared, real reboot, AP (`WindmeterTester-BEFC`) confirmed broadcasting, page loaded manually and showed every section |
| 2 | INT-02 | Configure WiFi via AP-mode web UI | No | Device reconnects in STA mode; `windmeter-tester.local` resolves | **Done, with a real bug found and fixed along the way** — see note below. Reconnect confirmed (STA, correct SSID, mDNS resolving); the new auto-reboot-on-save code path itself not yet independently re-verified, see note |
| 3 | INT-09 | Settings survive power-cycle | No | WiFi credentials, last scan range, last Wind Test address all retained | **Done** — WiFi credentials confirmed retained (STA reconnect + mDNS after a real `esptool` hard reset); scan range and Wind Test address found and fixed a real bug in the process, see note below |
| 4 | INT-08 | LED state transitions | No (or either) | Idle → scanning → valid-frame/error states are visually correct, return to idle when the scan ends | **Done** — visually confirmed against a real scan: steady blue idle → amber while scanning → green flash on the FG6485A's reply → back to amber → blue on completion |
| 5 | INT-03 | Full bus scan | Real FG6485A, addr 1 | Bus Scanner reports the address; progress visible throughout | **Done** |
| 6 | INT-07 | Modbus Log ordering | Either | TX and RX frames appear correctly interleaved and timestamped | **Done** — `GET /api/v1/log` confirmed chronological order, each TX immediately followed by its RX, content matches independently-observed `raw_tx`/`raw_rx` |
| 7 | INT-04 | Generic register read via Register Explorer | Real FG6485A | FC03 read (addr 1) returns values matching an independent read | **Done** — `/explorer/query` and `/api/v1/modbus` returned identical live humidity/temperature values; Modicon `40001`→raw `0x0000` confirmed through `/explorer/query` itself |
| 8 | INT-06 | Register Explorer write + read-back | Real FG6485A (alarm config, `0x000C`–`0x0013`) | FC16 write followed by FC03 read-back returns the written value | **Done** — see note below on the out-of-range write finding. Register restored to its original value (800) afterward |
| 9 | INT-05 | Wind Test decode accuracy | **Real DUT** | **Blocked — see Phase 4** | Not run |

**Correction:** this table originally assumed address 1 was
`greenhouse-Controller-Modbus-sensor-emulator` (the software stand-in) and
address 44 was its S200 persona. Address 1 is actually a genuine physical
FG6485A sensor — confirmed 2026-07-02 — and the address-44/S200 half of
INT-03/04 has been dropped from scope entirely (2026-07-02 decision — S200
will never be connected), not deferred pending hardware. INT-03/04/06/07
above are considered fully done at their new, real-hardware-only scope —
stronger evidence than the original plan anticipated (real commercial
hardware, not a software stand-in), just narrower (one device, not two).

**INT-06 finding:** the first write attempt used an out-of-range test
value (4660, i.e. 466.0°C for a temperature-alarm-threshold register) and
silently did not stick — read-back kept returning the register's prior
value. Byte-level inspection (`raw_tx`/`raw_rx` via `/api/v1/modbus`)
confirmed the tester's FC16 frame was correct and the sensor's own success
echo was received and parsed correctly, so this was not a tester bug.
Retrying with a physically plausible value (500, 50.0°C) round-tripped
exactly. Working theory: the real sensor's own firmware validates/clamps
alarm-threshold writes to a sane range rather than raising a Modbus
exception for an out-of-range value — not confirmed against a datasheet,
but consistent with everything observed. Worth keeping in mind for any
future write-path testing: a "successful" FC16 echo does not guarantee the
value actually took effect on a real device.

INT-05 (and the DUT-specific half of INT-06, the Wind Test config
write-back) can't be meaningfully run against the FG6485A: its registers
use a different scale/layout than the DUT's actual Wind Test map (×10, 5
registers per `scratchbook.md` §5). A pass against it wouldn't validate
anything about the real device — that's why they wait for Phase 4 instead.

**INT-09 finding (real bug, fixed):** the scan range and Wind Test address
appeared to silently revert to their defaults after a reboot. Root cause
had nothing to do with rebooting specifically — `CFG_KEY_SCAN_RANGE_START`
(`"scan_range_start"`, 16 chars) and `CFG_KEY_WIND_POLL_INTERVAL`
(`"wind_poll_interval_ms"`, 21 chars) both exceeded Preferences' 15-char
NVS key limit, so every write to either had been silently failing all
along — not just across reboots, immediately. Every native test for these
two keys passed throughout, because `mock_cfg_backend` has no key-length
constraint to catch it. Fixed by shortening both keys and adding a native
test (`test_all_keys_fit_the_15_char_preferences_limit`) that checks every
`cfg_keys.h` constant directly — full writeup in `memory/gotcha-log.md`.
Re-verified after the fix: both settings now correctly survive a real
`esptool`-triggered hard reset (scan range confirmed via `GET /api/v1/scan`,
Wind Test address confirmed via the actual probed slave address in
`GET /api/v1/log`, since nothing is connected at that test address to
reply and prove it through `has_data`).

**INT-01/02 finding (real bug, fixed):** manually driven end-to-end —
joined the AP (`WindmeterTester-BEFC`, open), loaded `http://192.168.4.1`
(confirms INT-01: page loads with every section present), submitted real
WiFi credentials via the Settings section. Result: **the AP stayed up and
192.168.4.1 stayed the only way to reach the device** — no reconnect
happened. Root cause: `POST /config/wifi` saved the credentials to NVS
correctly (confirmed — they were sitting there, just unused) but
`wifi_manager_task` only evaluates NVS credentials once, at boot, and
nothing was triggering a boot. This was already a known, documented
limitation (§5 refinements table, previously filed as "requires a reboot
to take effect") — seeing it block the literal documented user flow
elevated it from polish to a real fix. Fixed: `/config/wifi`'s handler now
calls `ESP.restart()` (after a short delay so the `{"ok":true}` response
reaches the client first) instead of leaving the new credentials to sit
unused.

**What's proven vs. not yet re-verified:** flashing the fix itself
triggered a reboot, which picked up the already-pending credentials from
NVS and reconnected successfully (`GET /api/v1/status` afterward:
`"mode":"STA","ssid":"casaminerva_nomap"`, same IP, mDNS resolving) — so
the credentials-in/reconnect-out mechanism end-to-end is confirmed
working. What that specific reboot does *not* prove is the new
`ESP.restart()` call itself firing correctly from a fresh AP-mode submit
(that round's reboot came from reflashing over USB, not from the new
code path). Re-verifying that specifically would mean repeating the full
manual AP-join-and-submit flow once more against the now-fixed firmware —
low risk (`ESP.restart()` is a standard, well-established call, and the
underlying reboot→reconnect mechanism is now proven sound), but flagged
here rather than silently assumed.

### 3.3 What to watch for that native tests structurally can't catch

- **Real UART timing under load** — whether `mb_timeout_ms`'s default
  (200 ms) is comfortable against the emulator's actual response latency,
  not just the mocked-transport tests' instant replies.
- **Real bus contention** — `realisationPlan.md` §3's
  `test_serialises_concurrent_submitters` (a scan running while the Wind
  Test panel is also polling); native tests can exercise the queue's
  ordering logic but not real simultaneous submitters racing for the UART.
- **Exception-reply handling against a real slave** —
  `bus_scan_did_respond()`'s "an exception reply still counts as found"
  rule is verified in native tests against synthetic exception frames;
  confirm it holds against the emulator's real exception responses too.
- **Traffic-log timestamps once NTP is actually synced** during a real
  scan — native tests use a fixed/injected time base, so this is the first
  time real wall-clock timestamps flow through `mblog_append()`.

---

## 4. Phase 4 — Real DUT verification

**No longer blocked on the DUT firmware existing.** This section long said
"blocked on `windmeters-modbus-interface`'s firmware implementing its
register map... still scaffolding" — stale as of 2026-07-11:
`windmeters-modbus-interface/software/hil/testReport.md` shows all three
builds (speed, direction, combined) with an extensive, dated hardware
integration-test history (e.g. `CMB-REG-01` — full register matrix on a
real combined unit @ address 32, 77/77 assertions passing, 2026-07-08) run
against real silicon on the DUT team's own bench. What this section still
means, though, is **the tester's own INT-05/INT-06 acceptance tests
(defined in `completeRealisationPlan.md`) haven't been run through this
project's actual GUI/API against a physical unit** — the DUT team's HIL
suite is a different test harness proving the DUT's firmware is correct,
not a substitute for confirming *this tester* decodes/displays it
correctly end to end. That confirmation is what the rest of this section
still describes.

This now means **three** physical unit variants to verify (§2.5), not two:

1. Flash all three DUT variants (or one board, rejumpered between builds)
   with their register-map firmware.
2. Re-run `realisationPlan.md` §4 step 5: repeat bring-up steps 3/4 against
   the real DUT instead of the emulator.
3. Run **INT-05** (Wind Speed / Wind Direction / Wind Combined decode
   accuracy — three passes, one per type) — the first time `wind_poll`'s
   ×10 scale-factor decode, and the combined build's 13-register atomic
   snapshot, are checked against real register values instead of
   hand-constructed native-test fixtures.
4. Run the DUT-specific half of **INT-06** — holding-register config
   write-back via each type's own tab (Wind Speed: measurement window/
   averaging window/low-speed cutoff; Wind Direction: direction offset/
   averaging window; Wind Combined: all six, including confirming
   calibration_c/pulses_per_rotation persist across a DUT reset), not just
   the emulator's FG6485A stand-in. No device-address case — TDS v0.6
   (FR-MB07/FR-MB26) removed that register; the slave address is
   hardware-jumpered only.
5. **Regression pass whenever the DUT's register map changes upstream**
   (`scratchbook.md` §8 step 13). Re-check
   `windmeters-modbus-interface/design/TDS.md` isn't stale before trusting
   `wind_poll`'s register offsets again — it moved twice already (the
   physical-separation finding, then the v0.6 unification + combined
   build), and the DUT team's own HIL suite is worth a periodic skim for
   anything not yet reflected here.

---

## 5. Phase 5 — Known refinements (not blockers)

Gaps already documented as deferred, or noticed during Phase 2's hardware
verification:

| Item | Source | Why it's not a v1 blocker |
|---|---|---|
| ~~WiFi settings require a reboot to take effect~~ | Discovered during Phase 2 (TASK-WEB) | **Fixed 2026-07-02** — turned out to be a real INT-02 failure, not just polish (§3.2). `/config/wifi` now calls `ESP.restart()` after saving, instead of leaving new credentials unused until an unrelated reboot happened to apply them |
| Bus scan uses the same `mb_timeout_ms` as everything else — no scan-specific faster timeout | `completeRealisationPlan.md` §3, TASK-SCAN | Worst case ~1 minute for an empty 1–247 sweep; acceptable for v1, candidate refinement if it proves annoying in practice |
| Multi-device Wind dashboard (several devices live on screen at once) | `scratchbook.md` §9 — explicit v2 decision | v1 intentionally targets one address at a time per type. Not the same thing as §2.5's Wind Speed/Direction tab split — two tabs for two sensor *types*, still one active poll target |
| Register Explorer saved queries | `scratchbook.md` §9 — explicit backlog decision | Not needed to validate the DUT |

None of these block Phase 3 or Phase 4. Revisit after real-DUT verification,
when it's clearer which refinements actually matter in practice versus
which were theoretical concerns.

---

## 6. Housekeeping

- Phase 1/2/2.5, the Phase 3 integration-test fixes, and §2.5's wind
  speed/direction split + GUI restructure are all committed (`5affff4`).
  The log-broadcast TX-drop fix (§2.5) and this documentation pass are
  staged, not yet committed — next things to review.
- `memory/gotcha-log.md` is the first thing to check if Phase 3 turns up
  something that looks like a hardware fault — several "looked like a
  wiring problem, wasn't" cases already happened once this session (the
  UART glitch byte, WiFi event-log interleaving, a PowerShell HTTP-client
  false alarm). Update it again as Phase 3 surfaces new ones.
- Standing hygiene note from `progress.md` §7: keep real WiFi credentials
  (or any other real secret used for bench verification) out of tracked
  source, memory files, and documentation — provision via NVS directly
  (temporary source edit → flash → serial-readback confirm → revert → grep
  confirm clean, as done for TASK-WIFI/TASK-NTP) rather than hardcoding.

---

*End of What's Next*
