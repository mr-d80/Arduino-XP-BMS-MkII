# Changelog

All notable changes to this firmware are documented here.

## [Unreleased]

### Added

- Added USB `telemetry stats` counters for queued, submitted, skipped, and rejected Bluetooth frames and submitted bytes.

- Added logical charge/load output and voltage/temperature warning/shutdown state to every valid or unavailable telemetry packet.
- Added bounded module snapshots, full Modbus response validation, a dedicated telemetry UART, and no-dependency core logic tests.
- Added explicit one-to-eight module and ID 1-48 configuration limits.
- Mirrored coherent telemetry packets to native USB serial when a host is attached, enabling direct Android USB-host operation alongside Bluetooth.

### Changed

- Buffered Bluetooth reports as complete snapshots and paced Serial2 output in nonblocking 32-byte chunks every 80 ms; user testing found stable reception at 80/100 ms but recurring errors at 50 ms.

- Connects HC-06 directly at 38,400 baud on Serial2 TX 10 / RX 9, replacing the Nano passthrough and underside-pad connection. Bluetooth receive data is discarded pending future command support.
- Moves UVW and UVS indicators from pins 9/10 to pins 18/17; rewiring is required before uploading this firmware.
- Polls voltage and temperature before dashboard telemetry and combines the duplicate SOC/current request.
- Uses fixed-point values and emits coherent Android-compatible packets at one-second intervals.
- Keeps the legacy EEPROM event log isolated and disabled pending a record-format redesign.

### Fixed

- Prevented charge/load activation during startup testing and applied safety shutdowns even when later reads fail.
- Prevented alarm clearing from incomplete scans, recursive no-battery recovery, stale module data reuse, storage SOC scaling errors, discovery index corruption, serial-number truncation, unchecked CRC data, and output buffer overruns.
