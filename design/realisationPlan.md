# Realisation Plan — Modbus Master

## Windmeters Modbus Interface Tester

| Field        | Value                                    |
|--------------|-------------------------------------------|
| Document     | Realisation Plan                          |
| Component    | Modbus Master (engine + task)             |
| Version      | 0.1 (draft)                               |
| Date         | 2026-07-01                                |
| Status       | Draft                                     |
| Related docs | `design/scratchbook.md` (full tester design — this plan implements §6.2/§6.3) |

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [MB-1 — Modbus Master Core (library)](#2-mb-1--modbus-master-core-library)
3. [MB-2 — Modbus Master Task](#3-mb-2--modbus-master-task)
4. [Bring-Up and Verification Sequence](#4-bring-up-and-verification-sequence)
5. [Dependency Overview](#5-dependency-overview)
6. [Open Issues That Block Implementation](#6-open-issues-that-block-implementation)

---

## 1. Introduction

**Scope:** this plan covers only the Modbus master engine — the protocol
library (CRC16, frame build/parse, FC03/04/06/16) and the FreeRTOS task that
owns the RS-485 UART. It does **not** cover the Bus Scanner, Wind Test, or
Register Explorer web pages, WiFi/mDNS/NTP, NVS, or the RGB LED — those
consume this engine but are separate, already-sequenced work
(`scratchbook.md` §8, steps 3, 5–8, 10–11). If "the modbus master" was meant
to cover the whole tester, say so and this plan can be widened.

This plan refines `scratchbook.md` §6.3's API sketch into something directly
implementable — where the two disagree in a small way (e.g. the added
`mb_last_exception_code()` accessor below), this plan is the more current
one; the scratchbook's high-level shape still holds.

### Guiding principles

Adapted from greenhouse-Controller's own `implementationPlan.md`, since the
same discipline applies here:

- **Library first.** MB-1 is a standalone module with no FreeRTOS calls
  inside it — it doesn't know it's running in a task. This mirrors
  greenhouse-Controller's `drivers/modBus` (LIB-6), even though this project
  is Arduino framework, not ESP-IDF.
- **Unit test before integration.** CRC16 and frame encode/decode are pure
  functions over byte buffers — testable on the host with PlatformIO's
  native test runner, no hardware required. Only UART timing and the
  FreeRTOS task wrapper need target hardware.
- **Single bus owner.** Only MB-2 (`modbus_master_task`) ever touches the
  UART. The Bus Scanner, Wind Test poller, and Register Explorer all submit
  requests through MB-2's queue rather than calling MB-1 directly — this is
  the same rule `scratchbook.md` §6.2 already states, restated here because
  it drives MB-2's design.
- **No DE/RE state machine.** Confirmed in `scratchbook.md` §4.2: the
  Atomic RS485 Base auto-directions in hardware. MB-1 is simpler than
  greenhouse-Controller's `modbus_rtu.h` on this one point — there is no
  direction-control GPIO to sequence around each transaction.

---

## 2. MB-1 — Modbus Master Core (library)

**Description:** Modbus RTU master protocol engine on UART (`G5`=RX,
`G6`=TX per `scratchbook.md` §4.1, confirmed on the bench), 9600 8N1, no
DE/RE GPIO. Builds and sends FC03/04/06/16 requests, validates and decodes
responses (length, function code echo, slave address echo, CRC16), and
surfaces Modbus exception responses distinctly from transport errors.

**API surface:**

```c
typedef enum {
    MB_OK = 0,
    MB_ERR_TIMEOUT,     // no complete response within the configured timeout
    MB_ERR_CRC,         // response CRC did not match
    MB_ERR_EXCEPTION,   // slave returned a Modbus exception — see mb_last_exception_code()
    MB_ERR_FRAMING,     // wrong length, function code, or slave address echo
    MB_ERR_PARAM,       // bad argument (addr 0, count out of range, NULL out)
} mb_status_t;

void        mb_init(uint32_t baud, uint16_t timeout_ms, uint8_t retries);

mb_status_t mb_read_holding_registers   (uint8_t addr, uint16_t start, uint8_t count, uint16_t *out);
mb_status_t mb_read_input_registers     (uint8_t addr, uint16_t start, uint8_t count, uint16_t *out);
mb_status_t mb_write_single_register    (uint8_t addr, uint16_t reg,   uint16_t value);
mb_status_t mb_write_multiple_registers (uint8_t addr, uint16_t start, uint8_t count, const uint16_t *values);

uint8_t     mb_last_exception_code(void);           // valid only when the last call returned MB_ERR_EXCEPTION
uint16_t    mb_crc16(const uint8_t *buf, uint16_t len); // exposed for unit tests and the Register Explorer's raw-hex view
```

Argument validation (all functions): `addr` in 1–247 (0 = broadcast,
rejected — same convention as `s200.h`); `count` in 1–125 for reads, 1–123
for FC16; `out`/`values` non-NULL. Violations return `MB_ERR_PARAM` without
touching the wire.

Defaults carried over from `scratchbook.md` §6.3 / §7: 9600 baud, 200 ms
timeout, 1 retry — all three are runtime-configurable (`mb_init()` takes
them as arguments; MB-2 reads the current values from NVS on each call
rather than MB-1 hardcoding them).

**Task/thread compatibility:** No internal locking, no blocking delay
longer than the caller-supplied timeout, no global mutable state beyond a
single "last exception code" scratch variable (documented as
not-thread-safe — only meaningful to the caller that just got
`MB_ERR_EXCEPTION`, which is why MB-2 must consume it immediately, before
the next queued transaction runs).

**Unit tests** (host-runnable via `pio test -e native`, no hardware):

- `test_crc16_matches_reference` — verify `mb_crc16()` against known Modbus
  reference frames (cross-check against `drivers/modBus`'s own
  `modbus_crc16()` test vectors in greenhouse-Controller, since both
  implement the same polynomial 0xA001 / init 0xFFFF)
- `test_build_request_fc03` / `test_build_request_fc04` — verify request
  byte layout for known addr/start/count
- `test_build_request_fc06` — single-register write frame layout
- `test_build_request_fc16` — multi-register write frame layout, including
  the byte-count field and payload ordering
- `test_parse_response_valid_fc03_fc04` — feed a valid response; verify
  register values decoded in correct order
- `test_parse_response_crc_error` — corrupt the last byte; verify
  `MB_ERR_CRC`
- `test_parse_response_exception` — feed a `function | 0x80` exception
  frame; verify `MB_ERR_EXCEPTION` and that `mb_last_exception_code()`
  returns the correct code
- `test_parse_response_wrong_function_code` — reply FC doesn't match the
  request; verify `MB_ERR_FRAMING`
- `test_parse_response_wrong_slave_echo` — reply address byte doesn't match
  the request (a real risk on a shared multi-drop bus); verify
  `MB_ERR_FRAMING`, not silently accepted
- `test_timeout_no_response` — stubbed UART returns nothing inside the
  window; verify `MB_ERR_TIMEOUT`
- `test_retry_recovers_from_one_timeout` — first attempt times out, second
  succeeds; verify overall `MB_OK` and that only one retry was spent
  (exhausting the configured retry budget still returns `MB_ERR_TIMEOUT`)
- `test_register_count_bounds` — count 0 and count > 125 (reads) / > 123
  (FC16) both return `MB_ERR_PARAM` without a wire transaction
- `test_broadcast_address_rejected` — `addr = 0` returns `MB_ERR_PARAM` for
  every function

---

## 3. MB-2 — Modbus Master Task

**Depends on:** MB-1 (Modbus Master Core)
**Depends on tasks:** none — this is a foundation task, like T1/T9 in
greenhouse-Controller's plan; the Bus Scanner, Wind Test poller, and
Register Explorer all depend on *it*, not the reverse.

**Implementation:**

- Owns UART/`Serial2` exclusively. No other code path calls `mb_*`
  directly.
- Blocks on a request queue: `mb_request_t { addr, fc, start, count,
  values[123], reply_to }`. `scan_task`, `wind_poll_task`, and the Register
  Explorer's HTTP handler all post here and wait on `reply_to` (a
  per-caller queue or semaphore) rather than opening the port themselves —
  this is what makes the "single bus owner" rule in §1 actually hold under
  concurrent callers.
- Executes exactly one transaction at a time, in submission order. No
  reordering, no batching — bus contention among callers has never been
  seen but the queue exists specifically because it will eventually be
  contended (a scan running while the Wind Test panel is also polling).
- Re-reads `mb_baud` / `mb_timeout_ms` / `mb_retries` from NVS before each
  transaction (or on a config-changed notification — implementation can
  pick whichever is simpler; correctness requirement is that a settings
  change takes effect on the *next* transaction, not mid-transaction).
- Appends every TX+RX frame (raw hex, decoded one-line summary, timestamp)
  to the traffic-log queue consumed by the Modbus Log page
  (`scratchbook.md` §7).
- Updates the bus health counters (`crc_errors`, `timeouts`,
  `last_exception`) that feed the WebSocket status push
  (`scratchbook.md` §7 JSON shape).

**Acceptance tests** (target hardware — AtomS3 + Atomic RS485 Base wired
up; a bus peer is required, see §4):

- `test_single_request_round_trip` — submit one read request from a single
  caller; verify the correct result comes back on `reply_to`
- `test_serialises_concurrent_submitters` — two callers submit
  simultaneously; verify neither request's bytes interleave on the wire
  (this is the test that actually exercises the reason the queue exists)
- `test_traffic_log_receives_every_frame` — verify the log queue gets one
  entry per TX and one per RX for each transaction, in order
- `test_bus_health_counters_increment` — force one timeout and one CRC
  error (physically disconnect / inject noise, or use the emulator target
  from §4 to send a deliberately corrupt frame); verify both counters
  increment and `last_exception` stays `null` for these two cases
- `test_config_change_takes_effect_next_transaction` — change
  `mb_timeout_ms` via NVS mid-run; verify the *next* transaction uses the
  new value, not the in-flight one

---

## 4. Bring-Up and Verification Sequence

| Step | Action | Pass criteria |
|------|--------|----------------|
| 1 | UART loopback: jumper `G5`↔`G6` directly (bypassing the RS-485 base); write a known byte pattern | Bytes read back match exactly — confirms the UART pins and baud before any Modbus logic is in the loop |
| 2 | Run MB-1's unit test suite on host (`pio test -e native`) | All CRC16 / frame-build / frame-parse tests pass — no hardware involved |
| 3 | Connect the Atomic RS485 Base; run MB-1 against **`greenhouse-Controller-Modbus-sensor-emulator`** (an existing AtomLite unit) as the bus peer instead of the DUT | FC03 read of the FG6485A registers (address 1) and FC04 read of the S200 registers (address 44) both return values matching what the emulator's web UI shows — see `s200.h` / emulator docs for the exact register addresses |
| 4 | Run MB-2's acceptance tests against the same emulator target | All §3 acceptance tests pass |
| 5 | Repeat step 3/4 against the real `windmeters-modbus-interface` DUT | **Currently blocked** — see §6. Re-run once that firmware implements its register map |

Step 3 is the one worth calling out: the DUT (`windmeters-modbus-interface`)
has no register-handling firmware yet (`software/firmware/src/main.c` is
still scaffolding), so there is nothing real to test the master against
today. The sibling `greenhouse-Controller-Modbus-sensor-emulator` project —
already built, already known-working, already documented in
`scratchbook.md` §2 — is a real Modbus RTU slave on the same physical-layer
family and can stand in as the master's first bus peer. This de-risks MB-1
and MB-2 well ahead of the DUT being ready, rather than leaving the whole
Modbus master untestable until someone else's firmware lands.

---

## 5. Dependency Overview

```
MB-1  Modbus Master Core (library, host-testable)
  └── MB-2  Modbus Master Task (FreeRTOS, target-testable)
         ├── scan_task                    (Bus Scanner — scratchbook §8 step 3)
         ├── wind_poll_task               (Wind Test   — scratchbook §8 step 5)
         └── Register Explorer HTTP handler (scratchbook §8 step 4)
```

Nothing in this plan depends on WiFi, the web server, or NVS being
finished first — MB-1/MB-2 can be built and verified (§4, steps 1–4) in
parallel with `scratchbook.md` §8 steps 6–11.

---

## 6. Open Issues That Block Implementation

| Issue | Blocks | Status |
|-------|--------|--------|
| `windmeters-modbus-interface` firmware has no register implementation yet (`main.c` scaffolding only) | Bring-up step 5 (real-DUT verification) only | **Open** — does not block MB-1, MB-2, or bring-up steps 1–4 (§4) |
| ~~AtomS3 RS485 base RX/TX pin assignment~~ | ~~MB-1 UART init~~ | **Closed** — confirmed `G5`=RX, `G6`=TX (`scratchbook.md` §4.1/§4.2/§9) |
| ~~PlatformIO board id~~ | ~~project scaffolding~~ | **Closed** — `m5stack-atoms3` confirmed |
| ~~Addressing convention (raw vs. Modicon)~~ | ~~MB-1 API~~, Register Explorer | **Closed** — raw 0-based canonical (`scratchbook.md` §5/§9); MB-1's API above already reflects this — every `start`/`reg` parameter is a raw address |

---

*End of Realisation Plan v0.1*
