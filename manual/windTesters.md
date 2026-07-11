# Wind Speed / Wind Direction / Wind Combined

The device under test ships as up to **three physically separate build
variants**: a cup anemometer (wind speed), a wind-vane potentiometer (wind
direction), and a **combined** build that serves both sensors from one
board behind one Modbus slave address. The tester has one tab per variant
— **Wind Speed**, **Wind Direction**, and **Wind Combined** — rather than
trying to guess which one you have plugged in, because which variant is on
the bench is a hardware/firmware choice, not something the tester can
detect on its own.

**Only one of the three can poll at a time.** Starting any one tab's
polling silently stops whichever of the other two was running — the
underlying firmware task only ever targets one address. If you switch tabs
and see Start enabled / Stop disabled even though you thought polling was
still running, that's the tester correctly reporting that a *different*
tab took over; the GUI corrects all three tabs' buttons automatically
within a second of the next update.

**All three variants implement the same register map, as far as it goes.**
Per the DUT's own Technical Design Specification, every build shares one
register layout at the same addresses — a register the active build's
sensor doesn't have just reads 0, rather than each build having its own
differently-shaped map. The one real difference: the **combined** build's
input map is one register longer than the single-sensor builds' (13
registers instead of 12), since it adds a direction raw-ADC register that
doesn't exist at all on the single-sensor builds (see the Wind Combined
section below). Each tab shows only the registers that are actually
meaningful for that build; the rest (the other sensor's fields on a
single-sensor build, plus device-level diagnostics like uptime and error
counters) exist at the same addresses and are reachable via [Register
Explorer](registerExplorer.md), just not surfaced on these tabs. **There is
no device-address register on any variant** — the Modbus address is set by
a hardware solder jumper only and can't be read or changed over Modbus.

## Wind Speed tab

Default slave address: **30** (or **35** if the unit is jumpered to its
alternate address).

**Register map** (shown as a reference table at the top of the tab):

| Register | Addr | Field | Scale |
|---|---|---|---|
| Input (FC04) | `0x0001` | speed_instant | raw ÷10, m/s |
| Input (FC04) | `0x0003` | speed_avg | raw ÷10, m/s |
| Input (FC04) | `0x0004` | raw_pulses (last window) | unscaled |
| Input (FC04) | `0x000A` | seconds_since_pulse | unscaled, s |
| Input (FC04) | `0x000B` | gust (max in current averaging window) | raw ÷10, m/s |
| Holding (FC03/06) | `0x0001` | measurement_window | unscaled, ms (100–60000) |
| Holding (FC03/06) | `0x0002` | averaging_window | unscaled, s (1–600) |
| Holding (FC03/06) | `0x0003` | low_speed_cutoff | raw ÷10, m/s (0–50) |

**Live polling:**

1. Set **Slave address** (default 30) and **Poll interval (ms)** (default
   1000).
2. Click **▶ Start**. The Speed and Raw cards below update on every poll:
   instantaneous and averaged speed (m/s), raw pulse count, gust (the
   fastest single window within the current averaging period), seconds
   since the last anemometer pulse, and the age (ms) of the last successful
   reading — a growing age means the device has stopped answering, not that
   the display froze.
3. Click **■ Stop** to suspend polling (no further bus traffic from this
   tab until started again).

**Config (holding registers):** each field has its own **Write** button —
writes are always single-field (FC06), never a bulk write of everything at
once, so a typo in one field can't clobber the others.

| Field | What it changes |
|---|---|
| Measurement window (ms) | How long the anemometer's pulse-counting window is |
| Averaging window (s) | The window used for the "average" reading |
| Low-speed cutoff (m/s) | Below this speed, the device reports 0 m/s instead of a noisy near-zero value |

Click **Refresh config from device** to read the current values back (FC03)
and populate the fields above with what's actually stored on the unit right
now.

## Wind Direction tab

Default slave address: **31** (or **36** if the unit is jumpered to its
alternate address).

**Register map:**

| Register | Addr | Field | Scale |
|---|---|---|---|
| Input (FC04) | `0x0000` | dir_instant | raw ÷10, ° (65535 = sensor fault) |
| Input (FC04) | `0x0002` | dir_avg | raw ÷10, ° (65535 = sensor fault) |
| Input (FC04) | `0x0004` | raw_adc (last conversion) | unscaled, 0–1023 |
| Holding (FC03/06) | `0x0000` | dir_offset | raw ÷10, ° (0–3599) |
| Holding (FC03/06) | `0x0002` | averaging_window | unscaled, s (1–600) |

**Live polling:** same Slave address / Poll interval / Start / Stop
controls as Wind Speed, above. The Direction card shows instantaneous and
averaged heading in degrees; the Raw card shows the last raw ADC conversion
and the age of the last successful reading.

**Sensor fault:** if the potentiometer wiper is floating (disconnected, or
a bad connection) for more than 2 seconds, the unit reports a fault instead
of a reading — the Direction card shows a red **Sensor fault** badge, and
the instant/average values hold their last real reading rather than
updating. This clears automatically once a valid reading resumes.

**Config (holding registers):**

| Field | What it changes |
|---|---|
| Direction offset (°) | Calibration offset applied to the raw potentiometer reading — use this to correct for how the vane is physically mounted |
| Averaging window (s) | The window used for the "average" reading |

Same **Write** (single-field, FC06) and **Refresh config from device**
(FC03 read-back) behaviour as Wind Speed.

## Wind Combined tab

One physical unit serving **both** sensors behind a single slave address —
not two units sharing a bus, one board. Default slave address: **32** (or
**37** if the unit is jumpered to its alternate address).

**Register map** — 13 input registers (one longer than either single-sensor
build's 12) plus 6 holding registers (the anemometer calibration pair below
also exists, inert, on a direction-only build):

| Register | Addr | Field | Scale |
|---|---|---|---|
| Input (FC04) | `0x0000` | dir_instant | raw ÷10, ° (65535 = sensor fault) |
| Input (FC04) | `0x0001` | speed_instant | raw ÷10, m/s |
| Input (FC04) | `0x0002` | dir_avg | raw ÷10, ° (65535 = sensor fault) |
| Input (FC04) | `0x0003` | speed_avg | raw ÷10, m/s |
| Input (FC04) | `0x0004` | raw_pulses (last window) | unscaled |
| Input (FC04) | `0x000A` | seconds_since_pulse | unscaled, s |
| Input (FC04) | `0x000B` | gust (max in current averaging window) | raw ÷10, m/s |
| Input (FC04) | `0x000C` | dir_raw_adc (last conversion) — **combined-only register** | unscaled, 0–1023 |
| Holding (FC03/06) | `0x0000` | dir_offset | raw ÷10, ° (0–3599) |
| Holding (FC03/06) | `0x0001` | measurement_window | unscaled, ms (100–60000) |
| Holding (FC03/06) | `0x0002` | averaging_window | unscaled, s (1–600) |
| Holding (FC03/06) | `0x0003` | low_speed_cutoff | raw ÷10, m/s (0–50) |
| Holding (FC03/06) | `0x0004` | calibration_c (anemometer) | raw ÷1000, m/rotation (1–6553 raw) |
| Holding (FC03/06) | `0x0005` | pulses_per_rotation (anemometer) | unscaled, 1–1000 |

The direction reading's raw ADC has to move to a new register (`0x000C`)
on this build specifically, because `0x0004` — where the direction-only
build puts its raw ADC — is where the combined build's speed pulse count
lives instead. Everything else lines up with the single-sensor maps.

**Live polling:** same Slave address / Poll interval / Start / Stop
controls as the other two tabs. One poll reads the whole 13-register block
in a single transaction, so the Speed and Direction cards below always
update together, from one atomic snapshot:

- **Speed card:** instantaneous and averaged speed (m/s), raw pulse count,
  gust, seconds since the last anemometer pulse.
- **Direction card:** instantaneous and averaged heading (°), raw ADC
  (from `0x000C`), and the age (ms) of the last successful reading. The
  same **Sensor fault** badge as the Wind Direction tab appears here too —
  the fault-detection logic is identical, just reading from this build's
  register layout instead.

**Config (holding registers):** all six fields have their own **Write**
button, same single-field-FC06 convention as the other tabs.

| Field | What it changes |
|---|---|
| Direction offset (°) | Calibration offset applied to the raw potentiometer reading |
| Measurement window (ms) | How long the anemometer's pulse-counting window is |
| Averaging window (s) | The window used for both sensors' "average" readings |
| Low-speed cutoff (m/s) | Below this speed, the device reports 0 m/s instead of a noisy near-zero value |
| Anemometer calibration C (m/rotation) | The anemometer's calibration factor — how far the cup travels per rotation. Shown here in real m/rotation; the register itself stores it as an integer in 0.001 m/rotation units (1–6553), so a written value like `0.980` becomes raw `980` on the wire. |
| Anemometer pulses/rotation | How many pulses the anemometer emits per full rotation (most read-relay anemometers: 1) |

Click **Refresh config from device** to read all six values back (FC03) and
populate the fields above with what's actually stored on the unit right
now — including confirming a calibration write actually persisted (the DUT
stores all six holding registers in non-volatile memory across resets).

## If a register doesn't behave as documented

The tables above mirror the DUT's Technical Design Specification, which is
more settled than earlier in the project but can still change. If a write
or read doesn't match what's documented here, use
[Register Explorer](registerExplorer.md) to probe the raw register
directly; it doesn't assume any fixed map and will keep working even if the
DUT's registers move.
