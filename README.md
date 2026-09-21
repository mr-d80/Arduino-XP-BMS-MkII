# Arduino XP BMS MkII

Safety-first Teensy 3.2 firmware for monitoring six-cell Valence XP modules and controlling charge/load enable outputs.

## Supported configuration

- Teensy 3.2
- One to eight six-cell modules
- Module IDs 1 through 48, discovered at startup
- Series system-voltage calculation
- RS485 on Serial1 pins 0/1 at 115,200 baud after the 9,600-baud wake message
- Direct HC-06 telemetry on Serial2 TX pin 10 / RX pin 9 at 38,400 baud, 8N1
- USB Serial at 115,200 baud for diagnostics, commands, and a mirror of the coherent telemetry packet when a host is attached

New module IDs require a restart. Previously discovered modules recover automatically after a communications interruption.

The firmware uses a 50 ms per-response timeout, waits at least 100 ms between complete scans, retries unsuccessful discovery after a one-second backoff, and limits telemetry to 1 Hz. Discovery rejects an installation if a ninth responding module is found; the control outputs remain disabled.

## Thresholds

All comparisons use fixed-point integer units. Assertion is strict (`>` for high thresholds and `<` for low thresholds); clearing includes the hysteresis boundary.

| Condition | Warning assertion | Shutdown assertion | Hysteresis clearing |
| --- | ---: | ---: | --- |
| Cell voltage | above 3.850 V | above 3.950 V | at or below 3.650/3.750 V |
| Cell temperature | above 60.00 C | above 65.00 C | at or below 58.00/63.00 C |
| PCBA temperature | above 80.00 C | above 85.00 C | at or below 78.00/83.00 C |
| Cell undervoltage | below 2.850 V | below 2.600 V | at or above 3.050/2.800 V |

Storage mode starts charging when any module is at or below 40.0% SOC and stops when a module reaches 50.0%, provided no module is still at or below 40.0%. The storage charging state never changes unless every discovered module has a valid same-scan SOC value.

## Safety behavior

Charge and load outputs are set to their inactive levels before their pins become outputs. They are never included in the startup lamp test and remain disabled until discovery is successful and one complete module scan passes.

Voltage and temperature reads for every module run before SOC/current and balance reads. A valid dangerous reading asserts its warning or shutdown immediately. Later communication failures cannot undo that assertion, and alarms only clear after every module supplies valid relevant readings within the configured hysteresis boundary.

Any incomplete four-transaction scan counts as a communication failure. Two consecutive incomplete scans assert communications shutdown and disable both charge and load. The communications warning remains latched until reset with the console command.

Before connecting high-energy hardware, bench-test the inactive/active electrical levels on pins 3 and 4 and verify every shutdown output with the charger and contactors disconnected.

## Telemetry wiring and format

This firmware requires the revised direct-HC-06 wiring; rewire before uploading. The Nano is no longer part of the link.

| Signal | Teensy connection |
| --- | --- |
| UV warning indicator (UVW), formerly pin 9 | Pin 18, through its existing LED resistor |
| UV shutdown indicator (UVS), formerly pin 10 | Pin 17, through its existing LED resistor |
| HC-06 RXD | Pin 10 (Serial2 TX) |
| HC-06 TXD | Pin 9 (Serial2 RX) |
| HC-06 GND | Common circuit ground |
| HC-06 breakout VCC | Regulated 5 V bus, only for a breakout rated for 5 V input |

Serial signals are direct 3.3 V logic: remove the Nano's voltage divider. A bare HC-06 module requires a 3.3 V supply instead. Keep the HC-06 configured at 38,400 baud; the firmware does not issue AT commands to change its baud rate. Incoming Bluetooth bytes are discarded in bounded batches; no remote commands are implemented. Pins 13, 15 and underside pad 31 are not used by this link. The original PDF schematic and board photos show the old indicator wiring; use the table above for this revision.

Telemetry is independent of the USB diagnostic level and is emitted at most once per second. A valid packet contains one row per battery:

Bluetooth uses a 2,048-byte frame buffer and submits at most 32 bytes to Serial2 every 80 ms. At 38,400 baud, 8N1, a full chunk takes approximately 8.34 ms on the wire. User testing found stable reception at 80 and 100 ms, but errors returned at 50 ms; this is a tested mitigation, not a universal HC-06 guarantee. The main loop services transmission without `delay()` or `flush()` so it can continue scanning batteries. Scans can lengthen the gaps. An in-progress frame is never overwritten: if the next report is due while it is pending, that Bluetooth report is skipped, while USB still reports the current snapshot. A six-module report of approximately 769 bytes takes at least 1.92 seconds from first to last chunk submission, so skipped report opportunities are expected. A buffer overflow discards the entire unsent frame and records an error. Baud rate, pin assignments, and packet contents are unchanged by pacing.

    Battery <id> <V1> <V2> <V3> <V4> <V5> <V6> <VT> <T1> <T2> <T3> <T4> <T5> <T6> <PCBA> <SOC> <CURRENT> <BAL>
    BMS Status: EC=<0|1> EL=<0|1> OVW=<0|1> OVS=<0|1> UVW=<0|1> UVS=<0|1> OTW=<0|1> OTS=<0|1>
    Total System Voltage: <volts>
    Minimum Voltage: <volts>

`EC` and `EL` are the logical charging/load enable states actually applied to the outputs; their values are independent of electrical pin inversion. The other fields are the latched overvoltage, undervoltage, and overtemperature warning/shutdown bits. A shutdown can therefore be reported at the same time as its warning.

The packet ends with a blank line. An incomplete scan emits the following frame so the Android app retains its last valid battery values, marks them stale, and still receives current controller status:

    Telemetry unavailable: incomplete scan
    BMS Status: EC=<0|1> EL=<0|1> OVW=<0|1> OVS=<0|1> UVW=<0|1> UVS=<0|1> OTW=<0|1> OTS=<0|1>
    Total System Voltage: unavailable
    Minimum Voltage: unavailable

That frame also ends with a blank line.

The same valid or unavailable frame is mirrored to the Teensy's native USB serial connection. This lets the Android app use a USB host/OTG connection directly alongside the HC-06 path. USB diagnostics can appear before a telemetry frame; consumers must frame on the blank line and ignore non-telemetry lines, as the Android parser does. Android app builds predating the status indicators ignore the new `BMS Status` row and continue parsing battery telemetry.

At debug level 2, each complete human-readable USB scan also includes the same `BMS Status` row before its summaries. This keeps every complete, parseable USB frame self-contained; it does not change the dedicated HC-06 telemetry stream.

## Console commands

- help
- telemetry stats
- debug 0, debug 1, debug 2, and debug 21
- debug 2 <seconds>
- mode normal and mode storage
- reset cw
- log read and log clear are reserved and report that event logging is disabled

`telemetry stats` prints boot-session counters: `queued` (complete frames accepted), `submitted` (frames fully handed to Serial2), `skipped` (report due while a previous frame was pending), `rejected` (empty or oversized frame), `bytes` (bytes handed to Serial2), and `pending` (0 or 1). Submission is not an acknowledgement from the HC-06 or phone. `rejected` should remain zero; `queued - submitted` should be zero or one. Some skips are possible when scans delay output, especially with eight modules. Counters wrap at 32 bits.

## Bluetooth loss bench test

Clean native USB with truncated Bluetooth rows narrows the problem to the Serial2/HC-06/Bluetooth route. Pacing is a testable mitigation; it does not establish which device lost the bytes.

1. Upload this build using the existing Teensy 3.2 / USB Serial / 96 MHz configuration. Keep the HC-06 at 38,400 baud and the direct pin-10 wiring.
2. Use `debug 1` on USB. Run the phone over Bluetooth for at least ten minutes, including disconnect/reconnect, and retain its raw data if warnings recur. Check that all expected module IDs, one status row, and both summaries arrive per report.
3. Read `telemetry stats` at the start and end. `rejected` must stay zero and `submitted` must keep advancing. These counters establish local progress only.
4. If truncation continues, passively capture TX10 at the HC-06 RXD input with a suitable 3.3 V UART receiver or logic analyzer, decoding 38,400 baud 8N1. Compare those exact bytes with the phone's raw stream. An intact UART capture localizes subsequent loss to the HC-06/Bluetooth/phone path; a truncated UART capture points back to the sender or electrical link. Do not connect another transmitter to TX10.

The host test exercises the actual queue with UART backpressure, short writes, rollover, buffer overflow, frame overlap, and simulated 83 ms scan pauses. Build `tests/host/PacedTelemetryTests.cpp` with C++14 and `tests/host` as an include directory, then run the resulting executable. The stub `Arduino.h` is only for host tests. The firmware must still be compiled against the real Teensy core and verified on hardware.

## EEPROM status

The debug level and operating mode remain stored at the original top two EEPROM settings addresses.

Event logging is deliberately disabled. The legacy implementation described records as 32 bytes but advanced its write pointer by 38 or 41 bytes depending on the event. It must receive a versioned record layout, bounds tests, and a migration policy before it can safely be restored.

## Verification

Compile the main sketch for Teensy 3.2 / 3.1 with USB Type Serial. The no-dependency test sketch under tests/BmsCoreTests covers known CRC data, short/long/corrupt response validation, signed register decoding, SOC scaling, threshold/hysteresis boundaries, incomplete-scan communication behavior, storage behavior, and rollover-safe timing.

The paced-transmission build uses 28,880 bytes of flash and 6,128 bytes of dynamic memory, leaving 59,408 bytes reported for local variables on Teensy 3.2. Hardware-only checks still required before deployment are output-level verification through reset/POST/discovery/comms loss, sparse and overflow discovery cases, delayed/corrupt response injection, before/after scan timing, safety assertion when a later dashboard read fails, relocated UV indicators, and both USB and direct HC-06 telemetry paths.
