# Development notes

## 2026-09-08 - Direct HC-06 connection

- UVW moves to GPIO 18 and UVS to GPIO 17, freeing the default Serial2 RX 9 / TX 10 pair. Both selected indicator pins were unused in the firmware and original schematic.
- Serial2 explicitly selects both pins and 38,400 baud 8N1, matching the existing HC-06 configuration. No Nano or underside-pad wiring is required.
- Bluetooth RX is connected for future work, but input is discarded with a bounded read count. USB remains the only console command interface.
- USB telemetry mirroring, safety thresholds, control outputs 3/4, and EEPROM settings are unchanged. The slower UART increases time spent transmitting a report; hardware scan timing still needs measurement.
- Original schematic/photo files are historical references; README contains the revised wiring. Bench verification of relocated indicators and real HC-06 telemetry remains required.
- Production compile passed with Teensy core 1.62.0, USB Serial, 96 MHz, Faster optimization: 26,924 bytes flash and 4,056 bytes dynamic RAM. No upload was performed.
