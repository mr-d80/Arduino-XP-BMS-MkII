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

    Battery <id> <V1> <V2> <V3> <V4> <V5> <V6> <VT> <T1> <T2> <T3> <T4> <T5> <T6> <PCBA> <SOC> <CURRENT> <BAL>
    Total System Voltage: <volts>
    Minimum Voltage: <volts>

The packet ends with a blank line. An incomplete scan emits the following frame so the current Android parser rejects the frame, retains its last valid values, and marks them stale:

    Telemetry unavailable: incomplete scan
    Total System Voltage: unavailable
    Minimum Voltage: unavailable

That frame also ends with a blank line.

The same valid or unavailable frame is mirrored to the Teensy's native USB serial connection. This lets the Android app use a USB host/OTG connection directly alongside the HC-06 path. USB diagnostics can appear before a telemetry frame; consumers must frame on the blank line and ignore non-telemetry lines, as the Android parser does.

## Console commands

- help
- debug 0, debug 1, debug 2, and debug 21
- debug 2 <seconds>
- mode normal and mode storage
- reset cw
- log read and log clear are reserved and report that event logging is disabled

## EEPROM status

The debug level and operating mode remain stored at the original top two EEPROM settings addresses.

Event logging is deliberately disabled. The legacy implementation described records as 32 bytes but advanced its write pointer by 38 or 41 bytes depending on the event. It must receive a versioned record layout, bounds tests, and a migration policy before it can safely be restored.

## Verification

Compile the main sketch for Teensy 3.2 / 3.1 with USB Type Serial. The no-dependency test sketch under tests/BmsCoreTests covers known CRC data, short/long/corrupt response validation, signed register decoding, SOC scaling, threshold/hysteresis boundaries, incomplete-scan communication behavior, storage behavior, and rollover-safe timing.

The current production build uses 26,924 bytes of flash and 4,056 bytes of dynamic memory, leaving 61,480 bytes reported for local variables on Teensy 3.2. Hardware-only checks still required before deployment are output-level verification through reset/POST/discovery/comms loss, sparse and overflow discovery cases, delayed/corrupt response injection, before/after scan timing, safety assertion when a later dashboard read fails, relocated UV indicators, and both USB and direct HC-06 telemetry paths.
