# Development notes

## 2026-10-03 - Combined balancing activity in telemetry

- User hardware captures resolved the rev. 1 reporting discrepancy: module 18 returned general status `0x0000`, bank byte `0x01` (active banks 2–6), and enabled state `0x0000`. Module 8 returned general status `0x0100` and bank byte `0x1D` (banks 2/6 active). Both earlier inactive captures had bank byte `0x3F`. This is module-reported activity, not a measurement of balancing current or proof across all firmware revisions.
- Telemetry `BAL` now uses the same validity-gated general-OR-bank decoder as diagnostics, derived from the current snapshot rather than a cached general-only flag. Failed SOC/current or general-status reads still prevent a complete numeric report. Diagnostics distinguish raw general activity from combined telemetry activity; the optional enable word does not gate activity reporting.
- No new transactions, balance-enable/calibration/control writes, threshold changes, output changes, transport changes, EEPROM changes, or Android code changes are needed. The final battery-row field remains numeric `0`/`1` and the row still contains 17 values. The previously documented general-only interpretation is superseded by this correction.
- Verification: Teensy core 1.62.0 / Teensy 3.2 / USB Serial / 96 MHz / Faster production compile passed at 32,308 bytes flash and 6,128 bytes RAM (32 bytes RAM less than the general-only diagnostic build). The core test sketch compiled at 11,692 bytes flash / 2,404 bytes RAM. All 83 core assertions, focused host diagnostic tests, and existing paced-telemetry regression tests passed. Regression tables include both user captures, inactive `0x3F`, general-only activity, upper-bit masking, and missing source responses. No hardware upload or end-to-end corrected telemetry capture was performed.
- Independent review found no blocking findings and confirmed that all USB/Bluetooth/debug row paths retain the 17-field contract, current-scan validity, and unchanged control behavior.

## 2026-10-02 - Read-only balancing investigation

- OEM static inspection revealed a broader activity indication than the original/current Arduino general `BAL` bit: it combines that bit with six active-low bank flags already present in the SOC/current response. OEM normal monitoring also sends enable-balancing and exit-calibration-mode commands, but their necessity/support on the user's rev. 1 module remains unconfirmed. No control writes are added here.
- `balance diag <id>` queues one discovered module for a USB-only capture after the next normal scan has committed safety/comms state and emitted telemetry. Raw general/bank data is retained only from that scan's validated reads; a separate bounded `0x005A` read reports enabled/disabled only when valid. Its failure is diagnostic-only and does not change the normal four-read completeness rule.
- Normal telemetry `BAL` intentionally retains the original general-bit meaning pending physical captures. OEM-style combined activity is diagnostic-only and unavailable if either source is invalid. The enable word is a later sample, not an atomic part of the scan. Capture procedure and evidence limits are in `docs/balancing_diagnostics.md`.
- Baseline production compile: Teensy core 1.62.0, Teensy 3.2 / USB Serial / 96 MHz / Faster; 28,880 bytes flash and 6,128 bytes RAM. Hardware verification and physical root cause remain outstanding; no upload or serial commands are performed by the agent.
- Verification: updated production compile passed at 32,304 bytes flash and 6,160 bytes RAM (+32 bytes RAM); core test sketch compile passed at 11,448 bytes flash and 2,404 bytes RAM. MSVC C++14 execution of the core sketch assertions, focused diagnostic decoder tests, and existing paced-telemetry regression tests all passed. The paced host build retains its pre-existing size_t-to-uint32_t warning. Independent source review found no blocking findings; the full sketch was not exercised against simulated or physical serial hardware.
- USB framing crosscheck: the diagnostic-only block uses no telemetry prefixes and is ignored by Android's framer. If a failed normal scan has already printed `Battery ... failed` lines before the report deadline, the diagnostic's blank terminator can close an error-only block and surface an Android protocol warning; no valid telemetry is replaced. This is limited to an explicit capture during a normal-read failure and resembles existing incomplete debug-table behavior.

## 2026-09-21 - Android receiver pacing validation

- Independent Bluetooth terminal captures on Nexus 6P and Pixel 6 also contained missing text, correcting the earlier suspicion of an XP BMS-only receive defect. Workstation PuTTY was clean. This does not establish the precise layer responsible for loss.
- User tested 100 ms chunk spacing successfully, observed errors again at 50 ms, and reports stable communication at 80 ms. Retained the user-selected 80 ms setting, 32-byte chunks, and 38,400 baud; no Android parser changes are part of this change.
- Six-module reports of approximately 769 bytes require 25 chunks, or at least 1.92 seconds from first to last submission, plus scan delays. Skipped report opportunities are therefore expected with the one-second reporting scheduler; they do not truncate the pending snapshot. Local submission counters remain distinct from remote delivery acknowledgements.
- Verification: host queue tests passed with an explicit 80 ms minimum-spacing assertion and simulated scan pauses; production Teensy 3.2 / USB Serial / 96 MHz / Faster compile passed (28,880 bytes flash, 6,128 bytes RAM). No upload was performed by the agent.

## 2026-09-19 - Paced Serial2 transmission for HC-06 loss investigation

- User confirmed direct Teensy-to-HC-06 wiring and clean USB messaging. Phone captures show missing chunks and frame boundaries on Bluetooth. HC-06 burst buffering is a hypothesis, not a confirmed diagnosis; physical UART capture remains the decisive localization step.
- Previously each report was written to Serial2 as a continuous burst and followed by `flush()`. Flush only waits for the local UART, not delivery to the Bluetooth receiver. The revised path formats an immutable frame into 2,048 bytes, then services up to 32 bytes every 20 ms from the main loop, checking available TX space. No telemetry delay/flush blocks battery scanning.
- The chunk interval includes the approximately 8.34 ms wire time at the unchanged 38,400 baud 8N1. Scan work may delay chunks further. A pending frame is completed before another is accepted; report opportunities while busy are counted and skipped. USB still emits the current report independently. Overflow rejects a whole frame before any bytes escape.
- USB `telemetry stats` exposes local queue/submission/skip/rejection/byte counters. Submission is not remote acknowledgement. No protocol fields, pin assignments, battery polling rules, or shutdown thresholds changed.
- Host C++ tests passed against the production queue, covering exact byte preservation, scan pauses, minimum chunk spacing including across frame boundaries, full-buffer backpressure, zero/short writes, immutable pending frames, capacity overflow/recovery, and millis rollover.
- Teensy core 1.62.0 production compile passed for Teensy 3.2, USB Serial, 96 MHz, Faster: 28,880 bytes flash and 6,128 bytes dynamic RAM. Existing operational-status changes in the working tree were preserved. No hardware upload or Bluetooth soak test was performed.

## 2026-09-10 - Operational status telemetry

- Both valid and unavailable packets now include one self-describing `BMS Status` row with `EC`, `EL`, `OVW`, `OVS`, `UVW`, `UVS`, `OTW`, and `OTS` Boolean values.
- `EC` and `EL` come from the committed logical output state after all shutdown and storage rules are applied, so electrical inversion of the charging output does not leak into the wire contract.
- The row is emitted after battery/unavailable content and before the two summary labels. The blank-line delimiter and battery row field count are unchanged, and older Android parsers ignore the additional row.
- Warning and shutdown bits remain independent. A shutdown frame can report both the associated warning and shutdown as active even though the physical warning LED is suppressed while its shutdown LED is lit.
- Complete debug-level-2 USB status tables also include the self-describing row so every parseable complete scan carries current controller state; human-readable diagnostics remain otherwise unchanged.
- Production compilation passed with Teensy core 1.62.0, USB Serial, 96 MHz, Faster optimization: 27,316 bytes flash and 4,056 bytes dynamic RAM. The core test sketch also compiles with the repository supplied as a local library.
- End-to-end hardware confirmation of status values over Bluetooth and USB remains required before upload.

## 2026-09-08 - Direct HC-06 connection

- UVW moves to GPIO 18 and UVS to GPIO 17, freeing the default Serial2 RX 9 / TX 10 pair. Both selected indicator pins were unused in the firmware and original schematic.
- Serial2 explicitly selects both pins and 38,400 baud 8N1, matching the existing HC-06 configuration. No Nano or underside-pad wiring is required.
- Bluetooth RX is connected for future work, but input is discarded with a bounded read count. USB remains the only console command interface.
- USB telemetry mirroring, safety thresholds, control outputs 3/4, and EEPROM settings are unchanged. The slower UART increases time spent transmitting a report; hardware scan timing still needs measurement.
- Original schematic/photo files are historical references; README contains the revised wiring. Bench verification of relocated indicators and real HC-06 telemetry remains required.
- Production compile passed with Teensy core 1.62.0, USB Serial, 96 MHz, Faster optimization: 26,924 bytes flash and 4,056 bytes dynamic RAM. No upload was performed.
