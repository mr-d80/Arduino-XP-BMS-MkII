/*
 * Arduino-XP-BMS-MkII
 *
 * Safety-first Valence XP battery monitor for Teensy 3.2.
 * Original project and attribution:
 *   https://github.com/J00ky/Arduino-XP-BMS-MkII
 *   https://github.com/seb303/Arduino-XP-BMS
 *   https://github.com/Crelex/Valance-Battery-Reader
 *
 * GPLv3. No warranty; validate all control outputs on a low-risk bench before
 * connecting a charger, load contactor, or high-voltage battery system.
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <stdlib.h>
#include <string.h>

#include "BmsCore.h"
#include "PacedTelemetry.h"

// The old event-log implementation used nominal 32-byte records but advanced
// by 38 or 41 bytes. It is deliberately unavailable until a versioned format
// and migration policy are designed. The two settings bytes remain compatible.
#define ENABLE_EEPROM_EVENT_LOG 0

#if ENABLE_EEPROM_EVENT_LOG
#error "EEPROM event logging is unsupported until its record format is redesigned."
#endif

namespace Config {

constexpr uint8_t kCellCount = 6;
constexpr uint8_t kMaximumModules = 8;
constexpr uint8_t kFirstModuleId = 1;
constexpr uint8_t kLastModuleId = 48;
constexpr uint32_t kConsoleBaud = 115200;
constexpr uint32_t kRs485Baud = 115200;
constexpr uint32_t kTelemetryBaud = 38400;
constexpr uint8_t kTelemetryTxPin = 10;
constexpr uint8_t kTelemetryRxPin = 9;
constexpr uint32_t kWakePauseMs = 500;
constexpr uint32_t kResponseTimeoutMs = 50;
constexpr uint32_t kScanPauseMs = 100;
constexpr uint32_t kTelemetryIntervalMs = 1000;
constexpr size_t kTelemetryFrameCapacity = 2048;
constexpr size_t kTelemetryChunkSize = 32;
// 32 bytes at 38400 baud 8N1 occupy 8.34 ms. Bench testing found
// 80/100 ms stable with Android receivers; 50 ms still lost characters.
constexpr uint32_t kTelemetryChunkIntervalMs = 80;
constexpr uint32_t kDiscoveryRetryMs = 1000;
constexpr long kMaximumDebugIntervalSeconds = 86400;
constexpr uint8_t kMaximumConsecutiveReadErrors = 2;

constexpr uint8_t kEnableChargingPin = 3;
constexpr uint8_t kEnableLoadPin = 4;
constexpr bool kInvertEnableCharging = true;
constexpr bool kInvertEnableLoad = false;
constexpr uint8_t kOverTemperatureWarningPin = 5;
constexpr uint8_t kOverTemperatureShutdownPin = 6;
constexpr uint8_t kOverVoltageWarningPin = 7;
constexpr uint8_t kOverVoltageShutdownPin = 8;
// Relocated indicators leave both Serial2 pins free for the direct HC-06 link.
constexpr uint8_t kUnderVoltageWarningPin = 18;
constexpr uint8_t kUnderVoltageShutdownPin = 17;
constexpr uint8_t kCommsWarningPin = 11;
constexpr uint8_t kCommsShutdownPin = 12;
constexpr uint8_t kRs485TxEnablePin = 2;
constexpr uint8_t kRs485TxPin = 1;
constexpr uint8_t kRs485RxPin = 0;

constexpr uint16_t kEepromSize = 2048;
constexpr uint16_t kEepromSettings = kEepromSize - 32;
constexpr uint8_t kInitialDebugLevel = 1;
constexpr uint8_t kInitialMode = 0;
constexpr int16_t kCellOverTemperatureWarning = 6000;
constexpr int16_t kCellOverTemperatureShutdown = 6500;
constexpr int16_t kPcbaOverTemperatureWarning = 8000;
constexpr int16_t kPcbaOverTemperatureShutdown = 8500;
constexpr int16_t kTemperatureHysteresis = 200;
constexpr uint16_t kCellOverVoltageWarning = 3850;
constexpr uint16_t kCellOverVoltageShutdown = 3950;
constexpr uint16_t kOverVoltageHysteresis = 200;
constexpr uint16_t kCellUnderVoltageWarning = 2850;
constexpr uint16_t kCellUnderVoltageShutdown = 2600;
constexpr uint16_t kUnderVoltageHysteresis = 200;
constexpr uint16_t kStorageMinimumSocTenths = 400;
constexpr uint16_t kStorageMaximumSocTenths = 500;

}  // namespace Config

enum StatusBit : uint8_t {
    STATUS_CS = 0,
    STATUS_CW = 1,
    STATUS_UVS = 2,
    STATUS_UVW = 3,
    STATUS_OVS = 4,
    STATUS_OVW = 5,
    STATUS_OTS = 6,
    STATUS_OTW = 7,
    STATUS_EL = 8,
    STATUS_EC = 9,
    STATUS_STC = 10,
    STATUS_ST = 11,
    STATUS_PO = 14
};

enum class TransactionError : uint8_t {
    None,
    Timeout,
    TooLong,
    Address,
    Function,
    ByteCount,
    Terminator,
    Crc
};

struct TransactionResult {
    TransactionError error;
    size_t received;

    bool ok() const {
        return error == TransactionError::None;
    }
};

struct ModuleSnapshot {
    uint8_t id;
    uint16_t cellMillivolts[Config::kCellCount];
    int16_t temperaturesCentiC[Config::kCellCount + 1];
    uint32_t moduleMillivolts;
    uint16_t socTenthsPercent;
    int16_t currentCentiAmps;
    uint8_t balance;
    bool voltageValid;
    bool temperatureValid;
    bool socCurrentValid;
    bool balanceValid;

    bool complete() const {
        return BmsCore::moduleScanComplete(
            voltageValid,
            temperatureValid,
            socCurrentValid,
            balanceValid
        );
    }
};

enum class RunState : uint8_t {
    Discovering,
    Running
};

#define Console Serial
#define Rs485 Serial1
#define Telemetry Serial2

const uint8_t kWakeMessage[] = {
    0x00, 0x00, 0x01, 0x01, 0xC0, 0x74, 0x0D, 0x0A, 0x00, 0x00
};
const uint8_t kReadVoltages[] = {
    0x00, 0x03, 0x00, 0x45, 0x00, 0x09, 0x00, 0x00, 0x0D, 0x0A
};
const uint8_t kReadTemperatures[] = {
    0x00, 0x03, 0x00, 0x50, 0x00, 0x07, 0x00, 0x00, 0x0D, 0x0A
};
const uint8_t kReadSocAndCurrent[] = {
    0x00, 0x03, 0x00, 0x39, 0x00, 0x0A, 0x00, 0x00, 0x0D, 0x0A
};
const uint8_t kReadBalance[] = {
    0x00, 0x03, 0x00, 0x1E, 0x00, 0x01, 0x00, 0x00, 0x0D, 0x0A
};
const uint8_t kReadModel[] = {
    0x00, 0x03, 0x00, 0xEE, 0x00, 0x01, 0x00, 0x00, 0x0D, 0x0A
};

constexpr size_t kVoltageResponseLength = 25;
constexpr size_t kTemperatureResponseLength = 21;
constexpr size_t kSocCurrentResponseLength = 27;
constexpr size_t kBalanceResponseLength = 9;
constexpr size_t kModelResponseLength = 9;
constexpr size_t kMaximumResponseLength = 27;

uint8_t g_moduleIds[Config::kMaximumModules] = {};
ModuleSnapshot g_snapshots[Config::kMaximumModules] = {};
uint8_t g_moduleCount = 0;
uint16_t g_status = static_cast<uint16_t>(1U << STATUS_CS);
uint8_t g_debugLevel = Config::kInitialDebugLevel;
uint8_t g_consecutiveReadErrors = 0;
bool g_debugOneShot = false;
bool g_configurationOverflow = false;
uint32_t g_debugIntervalMs = 0;
uint32_t g_lastDebugOutputMs = 0;
uint32_t g_lastTelemetryMs = 0;
uint32_t g_nextScanMs = 0;
uint32_t g_nextDiscoveryMs = 0;
uint32_t g_lastScanDurationMs = 0;
RunState g_runState = RunState::Discovering;
char g_consoleInput[32] = {};
size_t g_consoleInputLength = 0;
PacedTelemetry<Config::kTelemetryFrameCapacity, Config::kTelemetryChunkSize,
               Config::kTelemetryChunkIntervalMs> g_telemetryTx;

constexpr uint16_t statusMask(StatusBit bit) {
    return static_cast<uint16_t>(1U << static_cast<uint8_t>(bit));
}

bool statusSet(uint16_t status, StatusBit bit) {
    return (status & statusMask(bit)) != 0U;
}

void setStatus(uint16_t &status, StatusBit bit, bool enabled) {
    if (enabled) {
        status |= statusMask(bit);
    } else {
        status &= static_cast<uint16_t>(~statusMask(bit));
    }
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

void printFixed(Print &output, int32_t value, uint8_t decimalPlaces) {
    uint32_t magnitude;
    if (value < 0) {
        output.print('-');
        magnitude = static_cast<uint32_t>(-static_cast<int64_t>(value));
    } else {
        magnitude = static_cast<uint32_t>(value);
    }

    uint32_t scale = 1;
    for (uint8_t index = 0; index < decimalPlaces; ++index) {
        scale *= 10U;
    }

    output.print(magnitude / scale);
    if (decimalPlaces == 0) {
        return;
    }
    output.print('.');
    uint32_t remainder = magnitude % scale;
    for (uint32_t divisor = scale / 10U; divisor > 1U; divisor /= 10U) {
        if (remainder < divisor) {
            output.print('0');
        }
    }
    output.print(remainder);
}

void printPaddedUnsigned(Print &output, uint32_t value, uint8_t width) {
    uint32_t divisor = 1;
    for (uint8_t index = 1; index < width; ++index) {
        divisor *= 10U;
    }
    while (divisor > 1U && value < divisor) {
        output.print('0');
        divisor /= 10U;
    }
    output.print(value);
}

void printStatusBits(Print &output, uint16_t status) {
    const StatusBit bits[] = {
        STATUS_ST, STATUS_STC, STATUS_EC, STATUS_EL, STATUS_OTW, STATUS_OTS,
        STATUS_OVW, STATUS_OVS, STATUS_UVW, STATUS_UVS, STATUS_CW, STATUS_CS
    };
    const size_t count = sizeof(bits) / sizeof(bits[0]);
    for (size_t index = 0; index < count; ++index) {
        output.print(statusSet(status, bits[index]) ? '1' : '0');
        if (index + 1U < count) {
            output.print(F("    "));
        }
    }
    output.println();
}

void writeLogicalOutput(uint8_t pin, bool enabled, bool inverted) {
    const uint8_t level = enabled
        ? (inverted ? LOW : HIGH)
        : (inverted ? HIGH : LOW);
    digitalWrite(pin, level);
}

void writeStatusIndicators(uint16_t status) {
    const bool overVoltageShutdown = statusSet(status, STATUS_OVS);
    const bool underVoltageShutdown = statusSet(status, STATUS_UVS);
    const bool overTemperatureShutdown = statusSet(status, STATUS_OTS);
    const bool commsShutdown = statusSet(status, STATUS_CS);

    digitalWrite(
        Config::kOverVoltageWarningPin,
        statusSet(status, STATUS_OVW) && !overVoltageShutdown
    );
    digitalWrite(
        Config::kUnderVoltageWarningPin,
        statusSet(status, STATUS_UVW) && !underVoltageShutdown
    );
    digitalWrite(
        Config::kOverTemperatureWarningPin,
        statusSet(status, STATUS_OTW) && !overTemperatureShutdown
    );
    digitalWrite(
        Config::kCommsWarningPin,
        statusSet(status, STATUS_CW) && !commsShutdown
    );
    digitalWrite(Config::kOverVoltageShutdownPin, overVoltageShutdown);
    digitalWrite(Config::kUnderVoltageShutdownPin, underVoltageShutdown);
    digitalWrite(Config::kOverTemperatureShutdownPin, overTemperatureShutdown);
    digitalWrite(Config::kCommsShutdownPin, commsShutdown);
}

uint16_t applyControlState(uint16_t status) {
    const bool chargingAllowed =
        !statusSet(status, STATUS_OTS) &&
        !statusSet(status, STATUS_OVS) &&
        !statusSet(status, STATUS_CS) &&
        (!statusSet(status, STATUS_ST) || statusSet(status, STATUS_STC));
    const bool loadAllowed =
        !statusSet(status, STATUS_OTS) &&
        !statusSet(status, STATUS_UVS) &&
        !statusSet(status, STATUS_CS);

    writeLogicalOutput(
        Config::kEnableChargingPin,
        chargingAllowed,
        Config::kInvertEnableCharging
    );
    writeLogicalOutput(
        Config::kEnableLoadPin,
        loadAllowed,
        Config::kInvertEnableLoad
    );
    setStatus(status, STATUS_EC, chargingAllowed);
    setStatus(status, STATUS_EL, loadAllowed);
    writeStatusIndicators(status);
    return status;
}

void reportStatusChange(uint16_t oldStatus, uint16_t newStatus, uint8_t triggerId) {
    if (g_debugLevel == 0 || oldStatus == newStatus) {
        return;
    }
    Console.print(F("Status change"));
    if (triggerId != 0) {
        Console.print(F(" triggered by battery "));
        Console.print(triggerId);
    }
    Console.println();
    Console.println(F("                    ST   STC  EC   EL   OTW  OTS  OVW  OVS  UVW  UVS  CW   CS"));
    Console.print(F("Previous status:    "));
    printStatusBits(Console, oldStatus);
    Console.print(F("Current status:     "));
    printStatusBits(Console, newStatus);
}

void commitStatus(uint16_t candidate, uint8_t triggerId = 0) {
    const uint16_t oldStatus = g_status;
    candidate = applyControlState(candidate);
    g_status = candidate;
    reportStatusChange(oldStatus, candidate, triggerId);
}

void initializeOutputPins() {
    // Set the output latches to their inactive levels before enabling the pins.
    digitalWrite(
        Config::kEnableChargingPin,
        Config::kInvertEnableCharging ? HIGH : LOW
    );
    digitalWrite(
        Config::kEnableLoadPin,
        Config::kInvertEnableLoad ? HIGH : LOW
    );
    pinMode(Config::kEnableChargingPin, OUTPUT);
    pinMode(Config::kEnableLoadPin, OUTPUT);

    const uint8_t statusPins[] = {
        Config::kOverTemperatureWarningPin,
        Config::kOverTemperatureShutdownPin,
        Config::kOverVoltageWarningPin,
        Config::kOverVoltageShutdownPin,
        Config::kUnderVoltageWarningPin,
        Config::kUnderVoltageShutdownPin,
        Config::kCommsWarningPin,
        Config::kCommsShutdownPin
    };
    for (size_t index = 0; index < sizeof(statusPins); ++index) {
        digitalWrite(statusPins[index], LOW);
        pinMode(statusPins[index], OUTPUT);
    }
    commitStatus(g_status);
}

void runStatusLampTest() {
    const uint8_t statusPins[] = {
        Config::kOverTemperatureWarningPin,
        Config::kOverTemperatureShutdownPin,
        Config::kOverVoltageWarningPin,
        Config::kOverVoltageShutdownPin,
        Config::kUnderVoltageWarningPin,
        Config::kUnderVoltageShutdownPin,
        Config::kCommsWarningPin,
        Config::kCommsShutdownPin
    };
    for (size_t index = 0; index < sizeof(statusPins); ++index) {
        digitalWrite(statusPins[index], HIGH);
    }
    delay(250);
    for (size_t index = 0; index < sizeof(statusPins); ++index) {
        digitalWrite(statusPins[index], LOW);
    }
    delay(250);
    for (size_t index = 0; index < sizeof(statusPins); ++index) {
        digitalWrite(statusPins[index], HIGH);
        delay(100);
        digitalWrite(statusPins[index], LOW);
    }
    writeStatusIndicators(g_status);
}

void clearRs485Input() {
    while (Rs485.available() > 0) {
        Rs485.read();
    }
}

void writeToRs485(const uint8_t *data, size_t length) {
    Rs485.write(data, length);
    Rs485.flush();
}

TransactionResult transact(
    uint8_t moduleId,
    const uint8_t *requestTemplate,
    size_t requestLength,
    uint8_t expectedByteCount,
    uint8_t *response,
    size_t expectedResponseLength
) {
    if (requestLength > 10U || expectedResponseLength > kMaximumResponseLength) {
        return {TransactionError::TooLong, 0};
    }

    clearRs485Input();
    uint8_t request[10];
    memcpy(request, requestTemplate, requestLength);
    request[0] = moduleId;
    const size_t crcPosition = requestLength - 4U;
    const uint16_t crc = BmsCore::modbusCrc(request, crcPosition);
    request[crcPosition] = lowByte(crc);
    request[crcPosition + 1U] = highByte(crc);
    writeToRs485(request, requestLength);

    size_t received = 0;
    bool overflow = false;
    const uint32_t startedAt = millis();
    while (!BmsCore::intervalElapsed(millis(), startedAt, Config::kResponseTimeoutMs)) {
        while (Rs485.available() > 0) {
            const int nextByte = Rs485.read();
            if (received < expectedResponseLength) {
                response[received] = static_cast<uint8_t>(nextByte);
            } else {
                overflow = true;
            }
            ++received;
        }
        if (overflow || received > expectedResponseLength) {
            clearRs485Input();
            return {TransactionError::TooLong, received};
        }
        if (received == expectedResponseLength) {
            // Two character times catch a response longer than its declared shape.
            delayMicroseconds(250);
            if (Rs485.available() > 0) {
                while (Rs485.available() > 0) {
                    Rs485.read();
                    ++received;
                }
                return {TransactionError::TooLong, received};
            }
            break;
        }
        yield();
    }

    if (received != expectedResponseLength) {
        clearRs485Input();
        return {TransactionError::Timeout, received};
    }
    switch (
        BmsCore::validateResponse(
            response,
            received,
            expectedResponseLength,
            moduleId,
            expectedByteCount
        )
    ) {
        case BmsCore::ResponseError::None:
            return {TransactionError::None, received};
        case BmsCore::ResponseError::TooShort:
            return {TransactionError::Timeout, received};
        case BmsCore::ResponseError::TooLong:
            return {TransactionError::TooLong, received};
        case BmsCore::ResponseError::Address:
            return {TransactionError::Address, received};
        case BmsCore::ResponseError::Function:
            return {TransactionError::Function, received};
        case BmsCore::ResponseError::ByteCount:
            return {TransactionError::ByteCount, received};
        case BmsCore::ResponseError::Terminator:
            return {TransactionError::Terminator, received};
        case BmsCore::ResponseError::Crc:
            return {TransactionError::Crc, received};
    }
    return {TransactionError::Crc, received};
}

const __FlashStringHelper *transactionErrorText(TransactionError error) {
    switch (error) {
        case TransactionError::Timeout:
            return F("timeout/short response");
        case TransactionError::TooLong:
            return F("overlong response");
        case TransactionError::Address:
            return F("wrong module address");
        case TransactionError::Function:
            return F("wrong function");
        case TransactionError::ByteCount:
            return F("wrong byte count");
        case TransactionError::Terminator:
            return F("missing CR/LF");
        case TransactionError::Crc:
            return F("CRC mismatch");
        case TransactionError::None:
        default:
            return F("none");
    }
}

void reportTransactionError(
    uint8_t moduleId,
    const __FlashStringHelper *operation,
    const TransactionResult &result
) {
    if (g_debugLevel == 0) {
        return;
    }
    Console.print(F("Battery "));
    Console.print(moduleId);
    Console.print(' ');
    Console.print(operation);
    Console.print(F(" failed: "));
    Console.print(transactionErrorText(result.error));
    Console.print(F(" ("));
    Console.print(result.received);
    Console.println(F(" bytes)"));
}

void wakeUpBatteries() {
    clearRs485Input();
    Rs485.begin(9600, SERIAL_8N2);
    writeToRs485(kWakeMessage, sizeof(kWakeMessage));
    Rs485.begin(Config::kRs485Baud, SERIAL_8N2);
    delay(Config::kWakePauseMs);
}

void printModel(uint8_t modelCode, bool revisionTwo) {
    switch (modelCode) {
        case 49:
            Console.print(F("U1-12XP"));
            break;
        case 52:
            Console.print(F("U24-12XP"));
            break;
        case 55:
            Console.print(F("U27-12XP"));
            break;
        case 86:
            Console.print(F("UEV-18XP"));
            break;
        default:
            Console.print(F("Unknown model"));
            break;
    }
    Console.print(revisionTwo ? F(" Rev. 2") : F(" Rev. 1"));
}

bool discoverModules() {
    g_moduleCount = 0;
    g_configurationOverflow = false;
    if (g_debugLevel > 0) {
        Console.println(F("Searching for batteries (IDs 1-48)..."));
    }

    for (
        uint16_t address = Config::kFirstModuleId;
        address <= Config::kLastModuleId;
        ++address
    ) {
        uint8_t modelResponse[kModelResponseLength] = {};
        const TransactionResult modelResult = transact(
            static_cast<uint8_t>(address),
            kReadModel,
            sizeof(kReadModel),
            0x02,
            modelResponse,
            sizeof(modelResponse)
        );
        if (!modelResult.ok()) {
            if (modelResult.error != TransactionError::Timeout) {
                reportTransactionError(
                    static_cast<uint8_t>(address),
                    F("model discovery"),
                    modelResult
                );
            }
            continue;
        }

        if (g_moduleCount >= Config::kMaximumModules) {
            g_configurationOverflow = true;
            if (g_debugLevel > 0) {
                Console.print(F("ERROR: More than "));
                Console.print(Config::kMaximumModules);
                Console.println(F(" modules were found; system remains fail-safe."));
            }
            continue;
        }

        const uint8_t moduleIndex = g_moduleCount++;
        g_moduleIds[moduleIndex] = static_cast<uint8_t>(address);
        const bool revisionTwo = modelResponse[4] != 0;

        if (g_debugLevel > 0) {
            Console.print(F("Found Battery ID "));
            Console.print(address);
            Console.print(F(", model "));
            printModel(modelResponse[3], revisionTwo);
        }

        uint8_t serialResponse[kSocCurrentResponseLength] = {};
        const TransactionResult serialResult = transact(
            static_cast<uint8_t>(address),
            kReadSocAndCurrent,
            sizeof(kReadSocAndCurrent),
            0x14,
            serialResponse,
            sizeof(serialResponse)
        );
        if (g_debugLevel > 0) {
            Console.print(F(", serial "));
            if (!serialResult.ok()) {
                Console.print(F("unavailable"));
            } else if (!revisionTwo) {
                Console.print(BmsCore::decodeUnsigned16(serialResponse[3], serialResponse[4]));
            } else {
                const uint8_t prefix =
                    static_cast<uint8_t>(
                        ((serialResponse[4] & 0xC0U) >> 6U) |
                        ((serialResponse[3] & 0x0FU) << 2U)
                    );
                const uint8_t middle = serialResponse[4] & 0x3FU;
                const uint16_t suffix =
                    BmsCore::decodeUnsigned16(serialResponse[5], serialResponse[6]);
                printPaddedUnsigned(Console, prefix, 2);
                printPaddedUnsigned(Console, middle, 2);
                printPaddedUnsigned(Console, suffix, 5);
            }
            Console.println();
        }
    }

    if (g_configurationOverflow) {
        return false;
    }
    if (g_moduleCount == 0) {
        if (g_debugLevel > 0) {
            Console.println(F("No batteries detected; outputs remain disabled."));
        }
        return false;
    }
    if (g_debugLevel > 0) {
        Console.print(F("Discovery complete. Modules: "));
        Console.println(g_moduleCount);
    }
    return true;
}

void resetSnapshots() {
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        memset(&g_snapshots[index], 0, sizeof(g_snapshots[index]));
        g_snapshots[index].id = g_moduleIds[index];
    }
}

void assertVoltageAlarms(const ModuleSnapshot &snapshot) {
    uint16_t candidate = g_status;
    for (uint8_t cell = 0; cell < Config::kCellCount; ++cell) {
        const uint16_t voltage = snapshot.cellMillivolts[cell];
        if (BmsCore::assertHigh(voltage, Config::kCellOverVoltageWarning)) {
            setStatus(candidate, STATUS_OVW, true);
        }
        if (BmsCore::assertHigh(voltage, Config::kCellOverVoltageShutdown)) {
            setStatus(candidate, STATUS_OVS, true);
        }
        if (BmsCore::assertLow(voltage, Config::kCellUnderVoltageWarning)) {
            setStatus(candidate, STATUS_UVW, true);
        }
        if (BmsCore::assertLow(voltage, Config::kCellUnderVoltageShutdown)) {
            setStatus(candidate, STATUS_UVS, true);
        }
    }
    commitStatus(candidate, snapshot.id);
}

void assertTemperatureAlarms(const ModuleSnapshot &snapshot) {
    uint16_t candidate = g_status;
    for (uint8_t cell = 0; cell < Config::kCellCount; ++cell) {
        const int16_t temperature = snapshot.temperaturesCentiC[cell];
        if (BmsCore::assertHigh(temperature, Config::kCellOverTemperatureWarning)) {
            setStatus(candidate, STATUS_OTW, true);
        }
        if (BmsCore::assertHigh(temperature, Config::kCellOverTemperatureShutdown)) {
            setStatus(candidate, STATUS_OTS, true);
        }
    }
    const int16_t pcbaTemperature =
        snapshot.temperaturesCentiC[Config::kCellCount];
    if (BmsCore::assertHigh(pcbaTemperature, Config::kPcbaOverTemperatureWarning)) {
        setStatus(candidate, STATUS_OTW, true);
    }
    if (BmsCore::assertHigh(pcbaTemperature, Config::kPcbaOverTemperatureShutdown)) {
        setStatus(candidate, STATUS_OTS, true);
    }
    commitStatus(candidate, snapshot.id);
}

void readVoltage(ModuleSnapshot &snapshot) {
    uint8_t response[kVoltageResponseLength] = {};
    const TransactionResult result = transact(
        snapshot.id,
        kReadVoltages,
        sizeof(kReadVoltages),
        0x12,
        response,
        sizeof(response)
    );
    if (!result.ok()) {
        reportTransactionError(snapshot.id, F("voltage read"), result);
        return;
    }

    uint32_t moduleMillivolts = 0;
    for (uint8_t cell = 0; cell < Config::kCellCount; ++cell) {
        const size_t responseIndex = 9U + static_cast<size_t>(cell) * 2U;
        snapshot.cellMillivolts[cell] =
            BmsCore::decodeUnsigned16(response[responseIndex], response[responseIndex + 1U]);
        moduleMillivolts += snapshot.cellMillivolts[cell];
    }
    snapshot.moduleMillivolts = moduleMillivolts;
    snapshot.voltageValid = true;
    assertVoltageAlarms(snapshot);
}

void readTemperature(ModuleSnapshot &snapshot) {
    uint8_t response[kTemperatureResponseLength] = {};
    const TransactionResult result = transact(
        snapshot.id,
        kReadTemperatures,
        sizeof(kReadTemperatures),
        0x0E,
        response,
        sizeof(response)
    );
    if (!result.ok()) {
        reportTransactionError(snapshot.id, F("temperature read"), result);
        return;
    }

    snapshot.temperaturesCentiC[Config::kCellCount] =
        BmsCore::decodeSigned16(response[3], response[4]);
    for (uint8_t cell = 0; cell < Config::kCellCount; ++cell) {
        const size_t responseIndex = 5U + static_cast<size_t>(cell) * 2U;
        snapshot.temperaturesCentiC[cell] =
            BmsCore::decodeSigned16(response[responseIndex], response[responseIndex + 1U]);
    }
    snapshot.temperatureValid = true;
    assertTemperatureAlarms(snapshot);
}

void readSocAndCurrent(ModuleSnapshot &snapshot) {
    uint8_t response[kSocCurrentResponseLength] = {};
    const TransactionResult result = transact(
        snapshot.id,
        kReadSocAndCurrent,
        sizeof(kReadSocAndCurrent),
        0x14,
        response,
        sizeof(response)
    );
    if (!result.ok()) {
        reportTransactionError(snapshot.id, F("SOC/current read"), result);
        return;
    }
    snapshot.socTenthsPercent = BmsCore::socTenthsPercent(response[16]);
    snapshot.currentCentiAmps = BmsCore::decodeSigned16(response[17], response[18]);
    snapshot.socCurrentValid = true;
}

void readBalanceState(ModuleSnapshot &snapshot) {
    uint8_t response[kBalanceResponseLength] = {};
    const TransactionResult result = transact(
        snapshot.id,
        kReadBalance,
        sizeof(kReadBalance),
        0x02,
        response,
        sizeof(response)
    );
    if (!result.ok()) {
        reportTransactionError(snapshot.id, F("balance read"), result);
        return;
    }
    snapshot.balance = response[3] & 0x01U;
    snapshot.balanceValid = true;
}

void clearRecoveredSafetyAlarms() {
    bool allVoltagesValid = true;
    bool allTemperaturesValid = true;
    uint16_t maximumCellVoltage = 0;
    uint16_t minimumCellVoltage = UINT16_MAX;
    int16_t maximumCellTemperature = INT16_MIN;
    int16_t maximumPcbaTemperature = INT16_MIN;

    for (uint8_t module = 0; module < g_moduleCount; ++module) {
        const ModuleSnapshot &snapshot = g_snapshots[module];
        allVoltagesValid = allVoltagesValid && snapshot.voltageValid;
        allTemperaturesValid = allTemperaturesValid && snapshot.temperatureValid;
        if (snapshot.voltageValid) {
            for (uint8_t cell = 0; cell < Config::kCellCount; ++cell) {
                maximumCellVoltage = max(maximumCellVoltage, snapshot.cellMillivolts[cell]);
                minimumCellVoltage = min(minimumCellVoltage, snapshot.cellMillivolts[cell]);
            }
        }
        if (snapshot.temperatureValid) {
            for (uint8_t cell = 0; cell < Config::kCellCount; ++cell) {
                maximumCellTemperature =
                    max(maximumCellTemperature, snapshot.temperaturesCentiC[cell]);
            }
            maximumPcbaTemperature = max(
                maximumPcbaTemperature,
                snapshot.temperaturesCentiC[Config::kCellCount]
            );
        }
    }

    uint16_t candidate = g_status;
    if (allVoltagesValid) {
        if (
            statusSet(candidate, STATUS_OVW) &&
            BmsCore::clearHigh(
                maximumCellVoltage,
                Config::kCellOverVoltageWarning,
                Config::kOverVoltageHysteresis
            )
        ) {
            setStatus(candidate, STATUS_OVW, false);
        }
        if (
            statusSet(candidate, STATUS_OVS) &&
            BmsCore::clearHigh(
                maximumCellVoltage,
                Config::kCellOverVoltageShutdown,
                Config::kOverVoltageHysteresis
            )
        ) {
            setStatus(candidate, STATUS_OVS, false);
        }
        if (
            statusSet(candidate, STATUS_UVW) &&
            BmsCore::clearLow(
                minimumCellVoltage,
                Config::kCellUnderVoltageWarning,
                Config::kUnderVoltageHysteresis
            )
        ) {
            setStatus(candidate, STATUS_UVW, false);
        }
        if (
            statusSet(candidate, STATUS_UVS) &&
            BmsCore::clearLow(
                minimumCellVoltage,
                Config::kCellUnderVoltageShutdown,
                Config::kUnderVoltageHysteresis
            )
        ) {
            setStatus(candidate, STATUS_UVS, false);
        }
    }

    if (allTemperaturesValid) {
        const bool warningClear =
            BmsCore::clearHigh(
                maximumCellTemperature,
                Config::kCellOverTemperatureWarning,
                Config::kTemperatureHysteresis
            ) &&
            BmsCore::clearHigh(
                maximumPcbaTemperature,
                Config::kPcbaOverTemperatureWarning,
                Config::kTemperatureHysteresis
            );
        const bool shutdownClear =
            BmsCore::clearHigh(
                maximumCellTemperature,
                Config::kCellOverTemperatureShutdown,
                Config::kTemperatureHysteresis
            ) &&
            BmsCore::clearHigh(
                maximumPcbaTemperature,
                Config::kPcbaOverTemperatureShutdown,
                Config::kTemperatureHysteresis
            );
        if (statusSet(candidate, STATUS_OTW) && warningClear) {
            setStatus(candidate, STATUS_OTW, false);
        }
        if (statusSet(candidate, STATUS_OTS) && shutdownClear) {
            setStatus(candidate, STATUS_OTS, false);
        }
    }
    commitStatus(candidate);
}

void updateStorageState() {
    if (!statusSet(g_status, STATUS_ST)) {
        return;
    }

    uint16_t socValues[Config::kMaximumModules] = {};
    bool valid[Config::kMaximumModules] = {};
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        socValues[index] = g_snapshots[index].socTenthsPercent;
        valid[index] = g_snapshots[index].socCurrentValid;
    }
    const BmsCore::StorageDecision decision = BmsCore::evaluateStorage(
        statusSet(g_status, STATUS_STC),
        socValues,
        valid,
        g_moduleCount,
        Config::kStorageMinimumSocTenths,
        Config::kStorageMaximumSocTenths
    );
    uint16_t candidate = g_status;
    if (decision.startCharging) {
        setStatus(candidate, STATUS_STC, true);
    } else if (decision.stopCharging) {
        setStatus(candidate, STATUS_STC, false);
    }
    commitStatus(candidate);
}

bool scanComplete() {
    if (g_moduleCount == 0) {
        return false;
    }
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        if (!g_snapshots[index].complete()) {
            return false;
        }
    }
    return true;
}

void updateCommunicationState(bool complete) {
    const BmsCore::CommunicationState state = BmsCore::evaluateCommunication(
        complete,
        g_consecutiveReadErrors,
        statusSet(g_status, STATUS_CW),
        Config::kMaximumConsecutiveReadErrors
    );
    g_consecutiveReadErrors = state.consecutiveFailures;
    uint16_t candidate = g_status;
    setStatus(candidate, STATUS_CW, state.warningLatched);
    setStatus(candidate, STATUS_CS, state.shutdown);
    commitStatus(candidate);
}

void emitBatteryRow(Print &output, const ModuleSnapshot &snapshot) {
    output.print(F("Battery "));
    output.print(snapshot.id);
    for (uint8_t cell = 0; cell < Config::kCellCount; ++cell) {
        output.print(' ');
        printFixed(output, snapshot.cellMillivolts[cell], 3);
    }
    output.print(' ');
    printFixed(output, static_cast<int32_t>(snapshot.moduleMillivolts), 3);
    for (uint8_t sensor = 0; sensor < Config::kCellCount + 1U; ++sensor) {
        output.print(' ');
        printFixed(output, snapshot.temperaturesCentiC[sensor], 2);
    }
    output.print(' ');
    printFixed(output, snapshot.socTenthsPercent, 1);
    output.print(' ');
    printFixed(output, snapshot.currentCentiAmps, 2);
    output.print(' ');
    output.println(snapshot.balance);
}

void emitOperationalStatus(Print &output) {
    output.print(F("BMS Status: EC="));
    output.print(statusSet(g_status, STATUS_EC) ? '1' : '0');
    output.print(F(" EL="));
    output.print(statusSet(g_status, STATUS_EL) ? '1' : '0');
    output.print(F(" OVW="));
    output.print(statusSet(g_status, STATUS_OVW) ? '1' : '0');
    output.print(F(" OVS="));
    output.print(statusSet(g_status, STATUS_OVS) ? '1' : '0');
    output.print(F(" UVW="));
    output.print(statusSet(g_status, STATUS_UVW) ? '1' : '0');
    output.print(F(" UVS="));
    output.print(statusSet(g_status, STATUS_UVS) ? '1' : '0');
    output.print(F(" OTW="));
    output.print(statusSet(g_status, STATUS_OTW) ? '1' : '0');
    output.print(F(" OTS="));
    output.println(statusSet(g_status, STATUS_OTS) ? '1' : '0');
}

uint32_t totalSystemMillivolts() {
    uint32_t total = 0;
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        total += g_snapshots[index].moduleMillivolts;
    }
    return total;
}

uint32_t minimumModuleMillivolts() {
    uint32_t minimumVoltage = UINT32_MAX;
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        minimumVoltage = min(minimumVoltage, g_snapshots[index].moduleMillivolts);
    }
    return minimumVoltage;
}

void emitValidTelemetryFrame(Print &output) {
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        emitBatteryRow(output, g_snapshots[index]);
    }
    emitOperationalStatus(output);
    output.print(F("Total System Voltage: "));
    printFixed(output, static_cast<int32_t>(totalSystemMillivolts()), 3);
    output.println();
    output.print(F("Minimum Voltage: "));
    printFixed(output, static_cast<int32_t>(minimumModuleMillivolts()), 3);
    output.println();
    output.println();
}

void emitValidTelemetry() {
    if (g_telemetryTx.beginFrame()) {
        emitValidTelemetryFrame(g_telemetryTx);
        if (!g_telemetryTx.finishFrame() && g_debugLevel > 0) {
            Console.println(F("Telemetry TX frame exceeded buffer; frame discarded."));
        }
    }
    // Mirror the exact packet to native USB when a host is attached. Diagnostics
    // may precede it, but the packet delimiter and summary labels remain intact.
    if (Console) {
        emitValidTelemetryFrame(Console);
    }
}

void emitUnavailableTelemetryFrame(Print &output) {
    output.println(F("Telemetry unavailable: incomplete scan"));
    emitOperationalStatus(output);
    output.println(F("Total System Voltage: unavailable"));
    output.println(F("Minimum Voltage: unavailable"));
    output.println();
}

void emitUnavailableTelemetry() {
    if (g_telemetryTx.beginFrame()) {
        emitUnavailableTelemetryFrame(g_telemetryTx);
        if (!g_telemetryTx.finishFrame() && g_debugLevel > 0) {
            Console.println(F("Telemetry TX frame exceeded buffer; frame discarded."));
        }
    }
    if (Console) {
        emitUnavailableTelemetryFrame(Console);
    }
}

void maybeEmitTelemetry(bool complete) {
    const uint32_t now = millis();
    if (!BmsCore::intervalElapsed(now, g_lastTelemetryMs, Config::kTelemetryIntervalMs)) {
        return;
    }
    g_lastTelemetryMs = now;
    if (complete) {
        emitValidTelemetry();
    } else {
        emitUnavailableTelemetry();
    }
}

bool shouldPrintDetailedScan() {
    if (g_debugLevel >= 2) {
        return true;
    }
    if (g_debugOneShot) {
        g_debugOneShot = false;
        g_lastDebugOutputMs = millis();
        return true;
    }
    if (
        g_debugIntervalMs > 0 &&
        BmsCore::intervalElapsed(millis(), g_lastDebugOutputMs, g_debugIntervalMs)
    ) {
        g_lastDebugOutputMs = millis();
        return true;
    }
    return false;
}

void printDetailedScan(bool complete) {
    Console.println(F("V1 V2 V3 V4 V5 V6 VT T1 T2 T3 T4 T5 T6 PCBA SOC CURRENT BAL"));
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        if (g_snapshots[index].complete()) {
            emitBatteryRow(Console, g_snapshots[index]);
        } else {
            Console.print(F("Battery "));
            Console.print(g_snapshots[index].id);
            Console.print(F(" incomplete: V="));
            Console.print(g_snapshots[index].voltageValid);
            Console.print(F(" T="));
            Console.print(g_snapshots[index].temperatureValid);
            Console.print(F(" SOC/I="));
            Console.print(g_snapshots[index].socCurrentValid);
            Console.print(F(" BAL="));
            Console.println(g_snapshots[index].balanceValid);
        }
    }
    Console.println(F("ST   STC  EC   EL   OTW  OTS  OVW  OVS  UVW  UVS  CW   CS"));
    printStatusBits(Console, g_status);
    Console.print(F("Scan complete: "));
    Console.print(complete ? F("yes") : F("no"));
    Console.print(F(", duration ms: "));
    Console.println(g_lastScanDurationMs);
    if (complete) {
        // A detailed USB scan is itself parseable telemetry. Include the same
        // self-describing status row so debug level 2 cannot momentarily erase
        // otherwise current relay/alarm state in downstream consumers.
        emitOperationalStatus(Console);
        Console.print(F("Total System Voltage: "));
        printFixed(Console, static_cast<int32_t>(totalSystemMillivolts()), 3);
        Console.println();
        Console.print(F("Minimum Voltage: "));
        printFixed(Console, static_cast<int32_t>(minimumModuleMillivolts()), 3);
        Console.println();
    }
    Console.println();
}

void performScan() {
    const uint32_t scanStartedAt = millis();
    resetSnapshots();
    // Collect every safety-critical value before dashboard-only data.
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        readVoltage(g_snapshots[index]);
    }
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        readTemperature(g_snapshots[index]);
    }
    clearRecoveredSafetyAlarms();
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        readSocAndCurrent(g_snapshots[index]);
    }
    for (uint8_t index = 0; index < g_moduleCount; ++index) {
        readBalanceState(g_snapshots[index]);
    }

    updateStorageState();
    const bool complete = scanComplete();
    updateCommunicationState(complete);
    g_lastScanDurationMs = static_cast<uint32_t>(millis() - scanStartedAt);
    if (shouldPrintDetailedScan()) {
        printDetailedScan(complete);
    }
    maybeEmitTelemetry(complete);

    if (!complete && statusSet(g_status, STATUS_CS)) {
        if (g_debugLevel > 0) {
            Console.println(F("Communications shutdown active; waking battery bus."));
        }
        wakeUpBatteries();
    }
}

void saveDebugSetting() {
    EEPROM.update(Config::kEepromSettings, g_debugLevel);
}

void saveModeSetting(bool storageMode) {
    EEPROM.update(Config::kEepromSettings + 1U, storageMode ? 1U : 0U);
}

void printHelp() {
    Console.println(F("Available commands:"));
    Console.println(F("help         - show available commands"));
    Console.println(F("telemetry stats - show local Bluetooth transmit counters"));
    Console.println(F("debug 0      - turn off diagnostic output"));
    Console.println(F("debug 1      - errors and status changes"));
    Console.println(F("debug 2      - continuous complete scan output"));
    Console.println(F("debug 21     - show one complete scan"));
    Console.println(F("debug 2 <n>  - show a complete scan every n seconds"));
    Console.println(F("mode normal  - enter normal mode"));
    Console.println(F("mode storage - enter storage mode"));
    Console.println(F("reset cw     - clear the latched communications warning"));
    Console.println(F("log read     - reserved; EEPROM event logging is disabled"));
    Console.println(F("log clear    - reserved; EEPROM event logging is disabled"));
}

void handleConsoleCommand(const char *command) {
    if (strcmp(command, "telemetry stats") == 0) {
        Console.print(F("Telemetry TX: queued="));
        Console.print(g_telemetryTx.queuedFrames);
        Console.print(F(" submitted="));
        Console.print(g_telemetryTx.submittedFrames);
        Console.print(F(" skipped="));
        Console.print(g_telemetryTx.skippedFrames);
        Console.print(F(" rejected="));
        Console.print(g_telemetryTx.rejectedFrames);
        Console.print(F(" bytes="));
        Console.print(g_telemetryTx.transmittedBytes);
        Console.print(F(" pending="));
        Console.println(g_telemetryTx.pending() ? 1 : 0);
    } else if (strcmp(command, "debug 0") == 0) {
        g_debugLevel = 0;
        g_debugIntervalMs = 0;
        g_debugOneShot = false;
        saveDebugSetting();
        Console.println(F("debug 0"));
    } else if (strcmp(command, "debug 1") == 0) {
        g_debugLevel = 1;
        g_debugIntervalMs = 0;
        g_debugOneShot = false;
        saveDebugSetting();
        Console.println(F("debug 1"));
    } else if (strcmp(command, "debug 2") == 0) {
        g_debugLevel = 2;
        g_debugIntervalMs = 0;
        g_debugOneShot = false;
        saveDebugSetting();
        Console.println(F("debug 2"));
    } else if (strcmp(command, "debug 21") == 0) {
        g_debugLevel = 1;
        g_debugIntervalMs = 0;
        g_debugOneShot = true;
        saveDebugSetting();
        Console.println(F("debug 21"));
    } else if (strncmp(command, "debug 2 ", 8) == 0) {
        char *end = nullptr;
        const long intervalSeconds = strtol(command + 8, &end, 10);
        if (
            end != command + 8 &&
            *end == '\0' &&
            intervalSeconds > 0 &&
            intervalSeconds <= Config::kMaximumDebugIntervalSeconds
        ) {
            g_debugLevel = 1;
            g_debugIntervalMs = static_cast<uint32_t>(intervalSeconds) * 1000U;
            g_debugOneShot = true;
            saveDebugSetting();
            Console.print(F("debug 2 "));
            Console.println(intervalSeconds);
        } else {
            Console.println(F("debug interval must be 1..86400 seconds"));
        }
    } else if (strcmp(command, "mode normal") == 0) {
        uint16_t candidate = g_status;
        setStatus(candidate, STATUS_ST, false);
        setStatus(candidate, STATUS_STC, false);
        commitStatus(candidate);
        saveModeSetting(false);
        Console.println(F("mode normal"));
    } else if (strcmp(command, "mode storage") == 0) {
        uint16_t candidate = g_status;
        setStatus(candidate, STATUS_ST, true);
        setStatus(candidate, STATUS_STC, false);
        commitStatus(candidate);
        saveModeSetting(true);
        Console.println(F("mode storage"));
    } else if (strcmp(command, "reset cw") == 0) {
        uint16_t candidate = g_status;
        setStatus(candidate, STATUS_CW, false);
        commitStatus(candidate);
        Console.println(F("reset cw"));
    } else if (strcmp(command, "log read") == 0 || strcmp(command, "log clear") == 0) {
        Console.println(F("EEPROM event logging is disabled pending a record-format redesign."));
    } else if (strcmp(command, "help") == 0) {
        printHelp();
    } else if (command[0] != '\0') {
        Console.print(F("Unrecognised command: '"));
        Console.print(command);
        Console.println('\'');
        Console.println(F("Enter 'help' to show available commands."));
    }
}

void processConsoleInput() {
    while (Console.available() > 0) {
        const char next = static_cast<char>(Console.read());
        if (next == '\r' || next == '\n') {
            g_consoleInput[g_consoleInputLength] = '\0';
            handleConsoleCommand(g_consoleInput);
            g_consoleInputLength = 0;
            continue;
        }
        if (g_consoleInputLength + 1U >= sizeof(g_consoleInput)) {
            g_consoleInputLength = 0;
            while (Console.available() > 0) {
                const char discarded = static_cast<char>(Console.read());
                if (discarded == '\r' || discarded == '\n') {
                    break;
                }
            }
            Console.println(F("Console command too long; input discarded."));
            continue;
        }
        g_consoleInput[g_consoleInputLength++] = next;
    }
}

void loadSettings() {
    const uint8_t savedDebugLevel = EEPROM.read(Config::kEepromSettings);
    g_debugLevel = savedDebugLevel <= 2U
        ? savedDebugLevel
        : Config::kInitialDebugLevel;
    const uint8_t savedMode = EEPROM.read(Config::kEepromSettings + 1U);
    const uint8_t mode = savedMode <= 1U ? savedMode : Config::kInitialMode;
    setStatus(g_status, STATUS_ST, mode == 1U);
    setStatus(g_status, STATUS_STC, false);
}

void attemptDiscovery() {
    uint16_t candidate = g_status;
    setStatus(candidate, STATUS_CS, true);
    commitStatus(candidate);
    if (g_debugLevel > 0) {
        Console.println(F("Wake up batteries / initialise communications"));
    }
    wakeUpBatteries();
    if (discoverModules()) {
        g_runState = RunState::Running;
        g_consecutiveReadErrors = 0;
        g_lastTelemetryMs = millis() - Config::kTelemetryIntervalMs;
        g_nextScanMs = millis();
    } else {
        g_runState = RunState::Discovering;
        g_nextDiscoveryMs = millis() + Config::kDiscoveryRetryMs;
        maybeEmitTelemetry(false);
    }
}

void setup() {
    Console.begin(Config::kConsoleBaud);
    delay(100);
    loadSettings();
    initializeOutputPins();
    runStatusLampTest();

    Rs485.transmitterEnable(Config::kRs485TxEnablePin);
    Rs485.setTX(Config::kRs485TxPin);
    Rs485.setRX(Config::kRs485RxPin);
    Rs485.begin(Config::kRs485Baud, SERIAL_8N2);
    Telemetry.setTX(Config::kTelemetryTxPin);
    Telemetry.setRX(Config::kTelemetryRxPin);
    Telemetry.begin(Config::kTelemetryBaud, SERIAL_8N1);

    if (g_debugLevel > 0) {
        Console.print(F("Starting with debug "));
        Console.print(g_debugLevel);
        Console.print(F(", mode "));
        Console.println(statusSet(g_status, STATUS_ST) ? F("storage") : F("normal"));
        Console.println(F("Control outputs are fail-safe until a complete scan succeeds."));
    }
    g_lastTelemetryMs = millis() - Config::kTelemetryIntervalMs;
    attemptDiscovery();
}

void loop() {
    g_telemetryTx.service(Telemetry, millis());
    processConsoleInput();
    // RX is wired for future features; Bluetooth cannot execute console commands.
    // Bound the discard work so incoming traffic cannot hold up the scan loop.
    for (uint8_t count = 0; count < 64 && Telemetry.available() > 0; ++count) {
        Telemetry.read();
    }
    const uint32_t now = millis();
    if (g_runState == RunState::Discovering) {
        if (deadlineReached(now, g_nextDiscoveryMs)) {
            attemptDiscovery();
        }
        yield();
        return;
    }
    if (deadlineReached(now, g_nextScanMs)) {
        performScan();
        g_nextScanMs = millis() + Config::kScanPauseMs;
    }
    yield();
}
