# What's Next — Windmeters Modbus Interface Tester

| Field        | Value                                    |
|--------------|-------------------------------------------|
| Document     | Next Phases                               |
| Project      | Windmeters Modbus Interface Tester        |
| Date         | 2026-07-01                                |
| Status       | Planning — none of the phases below have started |
| Related docs | `design/progress.md` (what's done so far), `design/completeRealisationPlan.md` §4 (Part C — Integration Test, the source table Phase 3 below reproduces), `design/realisationPlan.md` §4 (bring-up sequence, steps 3–5), `memory/gotcha-log.md` |

---

## 1. Overview

Phases 1 (Libraries) and 2 (FreeRTOS Tasks + hardware-plumbing verification)
are complete — see `progress.md`. Every remaining phase is one of:

- **Phase 3 — Integration Test.** Prove the tester actually talks Modbus to
  a real bus peer, not just that its own queues/tasks/UART/HTTP plumbing
  works in isolation. This is `completeRealisationPlan.md` Part C
  (INT-01…INT-09), entirely unstarted.
- **Phase 4 — Real DUT verification.** Repeat the Wind-Test-specific parts
  of Phase 3 against the actual `windmeters-modbus-interface` board once its
  firmware implements a register map. Currently blocked — reconfirmed today:
  that repo's `git log` still shows `3427df2 First scaffolding` as HEAD,
  `software/firmware/src/main.c` unchanged.
- **Phase 5 — Refinements.** A short backlog of gaps that are known,
  documented, and intentionally deferred — not blockers, but worth listing
  so they don't get lost.

None of these are expected to need new library or task *code* by default —
Phase 3 in particular is mostly about running the tester against real bus
peers and fixing whatever real Modbus traffic reveals that mocked/native
tests structurally couldn't catch (framing edge cases, timing under real
UART latency, real bus contention).

---

## 2. Phase 3 — Integration Test

### 2.1 Bench setup required

Two RS-485 bus peers, per `realisationPlan.md` §4's reasoning: the real DUT
has no register firmware yet, so `greenhouse-Controller-Modbus-sensor-emulator`
(an existing, already-working AtomLite Modbus RTU slave, present locally at
`../greenhouse-Controller-Modbus-sensor-emulator`) stands in wherever a test
doesn't specifically need the DUT's own register map.

- The tester itself: AtomS3 + Atomic RS485 Base, already flashed and
  verified through Phase 2.
- The sensor-emulator AtomLite unit, flashed with its own existing firmware,
  responding at address 1 (FG6485A registers) and address 44 (S200
  registers) — both already documented in that repo.
- A shared RS-485 bus: the tester's G5/G6 leads connect to the emulator's
  bus terminals. **Not** the G5↔G6 loopback jumper used for the original
  UART self-test — that jumper must come off first, or it will intercept
  the tester's own TX as a malformed echo instead of letting it reach the
  emulator.
- A laptop/PC on the same network as the tester (STA mode) for the
  WebSocket/HTTP-driven tests (INT-01, 02, 07, 08, 09).

### 2.2 Test sequence

`completeRealisationPlan.md` §4's table, reproduced here with a suggested
run order — cheapest/most-isolated first, so a wiring mistake on the shared
bus doesn't block progress on the tests that don't need it yet:

| Order | Test ID | Scenario | Needs a bus peer? | Validation |
|---|---|---|---|---|
| 1 | INT-01 | Cold boot, no NVS config | No | Device starts in AP mode; `index.html` loads with every section present |
| 2 | INT-02 | Configure WiFi via AP-mode web UI | No | Device reconnects in STA mode; `windmeter-tester.local` resolves |
| 3 | INT-09 | Settings survive power-cycle | No | WiFi credentials, last scan range, last Wind Test address all retained |
| 4 | INT-08 | LED state transitions | No (or either) | Idle → scanning → valid-frame/error states are visually correct, return to idle when the scan ends |
| 5 | INT-03 | Full bus scan | Emulator (addr 1, 44) | Bus Scanner reports both addresses; progress visible throughout |
| 6 | INT-07 | Modbus Log ordering | Either | TX and RX frames from a scan appear correctly interleaved and timestamped |
| 7 | INT-04 | Generic register read via Register Explorer | Emulator | FC03 read (addr 1, FG6485A) and FC04 read (addr 44, S200) both return values matching the emulator's own web UI |
| 8 | INT-06 | Register Explorer write + read-back | Emulator (FG6485A alarm config, `0x000C`–`0x0013`, mechanics-only stand-in) | FC16 write followed by FC03 read-back returns the written values — validates the write path generically |
| 9 | INT-05 | Wind Test decode accuracy | **Real DUT** | **Blocked — see Phase 4** |

INT-05 (and the DUT-specific half of INT-06, the Wind Test config
write-back) can't be meaningfully run against the emulator: its S200
registers use a different scale and layout (×1000, 7 registers) than the
DUT's actual Wind Test registers (×10, 5 registers per `scratchbook.md`
§5). A pass against the emulator on those two wouldn't validate anything
about the real device — that's why they wait for Phase 4 instead.

### 2.3 What to watch for that native tests structurally can't catch

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

## 3. Phase 4 — Real DUT verification

**Blocked on:** `windmeters-modbus-interface`'s firmware implementing its
register map. `software/firmware/src/main.c` is still scaffolding as of
this writing (`git log` on that repo: HEAD = `3427df2 First scaffolding`)
— there is nothing to test the Wind Test decode against yet.

Once unblocked:

1. Flash the DUT with its own register-map firmware.
2. Re-run `realisationPlan.md` §4 step 5: repeat bring-up steps 3/4 against
   the real DUT instead of the emulator.
3. Run **INT-05** (Wind Test decode accuracy) — the first time
   `wind_poll`'s ×10 scale-factor decode is checked against real register
   values instead of hand-constructed native-test fixtures.
4. Run the DUT-specific half of **INT-06** — holding-register config
   write-back (device address, direction offset, measurement window,
   averaging window) via the Wind Test panel's config form, not just the
   emulator's FG6485A stand-in.
5. **Regression pass whenever the DUT's register map changes upstream**
   (`scratchbook.md` §8 step 13). The register map was noted as "4 commits
   old and still moving" when this project started — re-check
   `windmeters-modbus-interface/design/scratchBook.md` isn't stale before
   trusting `wind_poll`'s register offsets again.

This phase has no fixed start date — it starts whenever the DUT repo's
firmware catches up. Worth checking that repo's state periodically rather
than assuming it's still scaffolding indefinitely.

---

## 4. Phase 5 — Known refinements (not blockers)

Gaps already documented as deferred, or noticed during Phase 2's hardware
verification:

| Item | Source | Why it's not a v1 blocker |
|---|---|---|
| WiFi settings require a reboot to take effect | Discovered during Phase 2 (TASK-WEB) | `wifi_manager_task` only evaluates NVS credentials once, at boot; a live reconnect-without-reboot path is a small, isolated change whenever it's worth doing |
| Bus scan uses the same `mb_timeout_ms` as everything else — no scan-specific faster timeout | `completeRealisationPlan.md` §3, TASK-SCAN | Worst case ~1 minute for an empty 1–247 sweep; acceptable for v1, candidate refinement if it proves annoying in practice |
| Multi-device Wind Test dashboard | `scratchbook.md` §9 — explicit v2 decision | v1 intentionally targets one address at a time |
| Register Explorer saved queries | `scratchbook.md` §9 — explicit backlog decision | Not needed to validate the DUT |

None of these block Phase 3 or Phase 4. Revisit after real-DUT verification,
when it's clearer which refinements actually matter in practice versus
which were theoretical concerns.

---

## 5. Housekeeping

- **Nothing in this repo is committed yet** (`progress.md` §7). Phase 1+2
  work (`firmware/`, `memory/`, both new design docs, `progress.md`, this
  file) is sitting in the working tree. Worth reviewing and committing as a
  logical set before Phase 3 starts, so bench-testing changes have a clean
  baseline to diff against.
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
