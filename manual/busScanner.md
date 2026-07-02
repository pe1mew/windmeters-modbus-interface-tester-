# Bus Scanner

Sweeps a range of Modbus slave addresses over RS-485 and reports which
ones respond, with which function codes, and how fast. Use this first when
wiring up an unfamiliar bus, or whenever you need to confirm a device is
actually reachable before chasing a problem elsewhere.

## Fields

| Field | Meaning |
|---|---|
| Address range | Start and end slave address to sweep, inclusive. Default 1–247 (the full valid Modbus range; 0 is broadcast and is never scanned). |
| ▶ Start | Begins the sweep. |
| ■ Stop | Cancels a sweep in progress. |

There is no address-range preset list — type the range you need. (An
earlier version of this tab had quick-select buttons for common ranges;
they were removed once the Wind Speed/Wind Direction tabs got their own
correct default addresses and the presets stopped being useful. S200 —
address 44 — is permanently out of scope for this tool and was never
worth a preset regardless.)

## Running a scan

1. Set the start/end addresses.
2. Click **Start**. The line under the controls tracks progress: `Scanning
   address N / end (K found so far)`.
3. Wait for it to reach the end address, or click **Stop** to cancel
   early — the results table keeps whatever was found up to that point.
4. When done, the progress line reads `Complete — K address(es) found.`

A full default sweep (1–247) takes roughly a minute, since each address
gets one probe at the currently-configured Modbus timeout — see
[System Settings](systemSettings.md) if that feels too slow or too
aggressive for your bus.

## Reading the results

| Column | Meaning |
|---|---|
| Address | The slave address that responded |
| Functions | Which function code(s) it answered — e.g. `FC4` |
| Round trip | Time from request sent to reply received, in ms |

A device that replies with a **Modbus exception** (rather than staying
silent) still counts as found — it's alive on the bus, it just didn't like
that specific probe request. If a device you know is wired up doesn't show
up, check termination/bias resistors and A/B polarity before assuming it's
faulty; also worth a retry — the very first scan right after a fresh power-
on has occasionally missed a device that every scan since found reliably.

## Where to go next

Found an address you want to dig into? Use [Register Explorer](registerExplorer.md)
for a one-off read/write against it, or if it's one of the known wind
sensor addresses (30/35 for speed, 31/36 for direction), switch to the
matching [Wind Speed / Wind Direction](windTesters.md) tab instead.
