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

| Source | OEM interpretation | Existing telemetry |
| --- | --- | --- |
| `0x001E`, complete 16-bit status word | General activity is mask `0x0100` | The final battery-row `BAL` field continues to use this bit only |
| `0x0039`, full-response byte 15 | Bits 0 through 5 correspond to banks 1 through 6; a clear bit means active, so the active mask is `(~raw) & 0x3F` | Already received in the SOC/current transaction; now retained for diagnostics only |
| `0x005A`, complete 16-bit word | Mask `0x0010` clear means OEM balancing enabled; set means disabled | Optional diagnostic read only; no telemetry field is added |

The OEM-style combined activity indication is general activity OR any active
bank. It is available only when both source reads succeeded in the same scan.
These are interpretations observed in OEM software, not proof of physical
shunt current or verified behavior on every module firmware revision.

- Active banks with general `BAL=0` would explain an incomplete indication.
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
Android's blue-dot interpretation, the existing general `BAL` decoder, safety
outputs, EEPROM settings, and communications-failure rules remain unchanged.
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

The earlier original-Arduino parity check still holds for the general `BAL`
request and bit; it did not establish parity with the OEM's broader indication
or initialization sequence. Collect these read-only captures before proposing
any control writes or changing the telemetry meaning. Hardware capture and
revision-specific behavior are not established by compilation or host tests.
