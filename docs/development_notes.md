# Development notes

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
