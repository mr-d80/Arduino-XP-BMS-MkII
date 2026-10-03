# Read-only balancing diagnostics

These USB-console diagnostics distinguish missing balance indication from a
module reporting that balancing is disabled. They do not enable, disable, or
force balancing, exit calibration mode, or change charging thresholds.

## Capture procedure

1. Connect the Teensy's native USB port to a serial console at 115,200 baud.
   Keep the Teensy as the only RS485 master; do not connect the OEM diagnostic
   application or U-BMS as another master on the same bus.
2. Wait for discovery and normal scanning. Request `balance diag 8` (or another
   discovered ID from 1 through 48). Wait for its diagnostic block to finish
   before requesting `balance diag 18`. Only one request can be pending.
3. Repeat each command while the condition of interest is present and retain
   the complete USB output. Record module revision/model and firmware version
   if known. `debug 0` can reduce unrelated USB diagnostics; the requested
   diagnostic still prints. Restore your preferred debug level afterward.
4. Compare the raw words, bank flags, cell voltages/spread, and validity/errors
   for both revisions. Do not raise cell voltage or force balancing merely to
   provoke an indicator.

The request runs after the next normal safety/telemetry scan, not from the
console command handler. Register `0x005A` is sampled afterward, so its value is
not simultaneous with the earlier scan fields. Each field is decoded only from
a validated response; a missing field is unavailable, never a substitute zero
or a retained value from an earlier scan.

## Interpretation

| Source | OEM interpretation | Telemetry |
| --- | --- | --- |
| `0x001E`, complete 16-bit status word | General activity is mask `0x0100` | Contributes to the final battery-row `BAL` field |
| `0x0039`, full-response byte 15 | Bits 0 through 5 correspond to banks 1 through 6; a clear bit means active, so the active mask is `(~raw) & 0x3F` | Contributes to `BAL` using the existing SOC/current transaction |
| `0x005A`, complete 16-bit word | Mask `0x0010` clear means OEM balancing enabled; set means disabled | Optional diagnostic read only; no telemetry field is added |

The telemetry `BAL` and OEM-style combined diagnostic indication are general
activity OR any active bank. They use the same decoder and are available only
when both source reads succeeded in the same scan. The diagnostic separately
shows the raw general activity, which can remain zero while `BAL` is one.
These are interpretations observed in OEM software, not proof of physical
shunt current or verified behavior on every module firmware revision.

- Active banks with a zero general flag explain why older general-only `BAL`
  firmware missed the balancing indication.
- A validated enable word with the inhibit bit set is evidence of an inhibited
  state; a failed read is not evidence that balancing is disabled.
- Enabled with no reported activity does not establish the reason. Module
  firmware, conditions/history, and physical hardware remain to be checked.

## Scope and safety invariants

Normal polling remains four reads per module. Diagnostic capture retains the
already-polled raw status and bank byte and adds exactly one requested function
`0x03` read of register `0x005A`, count one, through the existing bounded,
CRC/envelope-validating transaction helper. That read can add up to the existing
50 ms timeout, plus USB print time, to the selected scan loop. An optional-read
failure does not count as an incomplete normal scan or change output/alarm state.

All diagnostic output is USB-only. Bluetooth pacing, the 17-value battery row,
Android's blue-dot interpretation, safety outputs, EEPROM settings, and
communications-failure rules remain unchanged. Only the reported `BAL` activity
meaning is broadened to match the OEM-style combined indication.
There is no automatic enable-balancing or calibration-mode write in this change.

The diagnostic-only block is ignored by Android's USB framer. During a failed
normal scan, pre-existing `Battery ... failed` console lines can be closed by
the diagnostic's blank delimiter before the next telemetry report is due;
Android may then report a protocol warning for that error-only block. Use a PC
console for these captures and retain the normal-read errors as evidence.

## Evidence and remaining bench work

Static inspection of `ModuleDiagG2.exe` in the local Valence G2 package found the
bank-bit interpretation, the separate `0x005A` enable-state read, and normal
monitoring startup calls to enable balancing and exit calibration mode. The
executable SHA-256 was
`18B9F85B2AC405077E2882C75E0D35AF9FD8A22E50A46C68361E1B86396973EA`.
The sequence had no revision gate, but support/necessity on module 18 is not
established. OEM "Start Read" is therefore not a passive read-only comparison.

The earlier original-Arduino parity check still holds for the general request
and bit; it did not establish parity with the OEM's broader indication or
initialization sequence. On 2026-10-03 the user supplied these validated
diagnostic captures:

| Module | General status | Bank byte | Active bank mask | Combined `BAL` |
| --- | --- | --- | --- | --- |
| 8 (rev. 2), active | `0x0100` | `0x1D` | `0x22` (banks 2 and 6) | `1` |
| 18 (rev. 1), active | `0x0000` | `0x01` | `0x3E` (banks 2 through 6) | `1` |
| 8 and 18, earlier inactive captures | `0x0000` | `0x3F` | `0x00` | `0` |

All of these captures reported enable word `0x0000`. The module 18 capture
establishes a reporting discrepancy in the old general-only indication, not a
need for enable-balancing writes or independent proof of physical shunt current.
After uploading the correction, confirm module 18's normal USB/Bluetooth row
ends in `1` when bank flags are active and that Android displays its blue dot.
Compilation and decoder tests cannot establish that end-to-end hardware result.
