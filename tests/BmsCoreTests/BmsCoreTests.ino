#include <Arduino.h>

#include <BmsCore.h>

uint16_t failures = 0;

void expectTrue(bool condition, const __FlashStringHelper *name) {
    if (condition) {
        Serial.print(F("PASS: "));
    } else {
        Serial.print(F("FAIL: "));
        ++failures;
    }
    Serial.println(name);
}

void runCrcTests() {
    const uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    expectTrue(
        BmsCore::modbusCrc(request, sizeof(request)) == 0xCDC5,
        F("known Modbus CRC vector")
    );
}

void makeValidResponse(uint8_t *response) {
    const uint8_t initial[] = {0x02, 0x03, 0x02, 0x12, 0x34, 0x00, 0x00, 0x0D, 0x0A};
    memcpy(response, initial, sizeof(initial));
    const uint16_t crc = BmsCore::modbusCrc(response, 5);
    response[5] = static_cast<uint8_t>(crc & 0x00FFU);
    response[6] = static_cast<uint8_t>(crc >> 8U);
}

void runResponseValidationTests() {
    enum Mutation : uint8_t {
        VALID,
        SHORT,
        LONG,
        ADDRESS,
        FUNCTION,
        BYTE_COUNT,
        TERMINATOR,
        CRC
    };
    struct TestCase {
        Mutation mutation;
        BmsCore::ResponseError expected;
        const __FlashStringHelper *name;
    };
    const TestCase cases[] = {
        {VALID, BmsCore::ResponseError::None, F("valid response")},
        {SHORT, BmsCore::ResponseError::TooShort, F("short response")},
        {LONG, BmsCore::ResponseError::TooLong, F("long response")},
        {ADDRESS, BmsCore::ResponseError::Address, F("wrong address")},
        {FUNCTION, BmsCore::ResponseError::Function, F("wrong function")},
        {BYTE_COUNT, BmsCore::ResponseError::ByteCount, F("wrong byte count")},
        {TERMINATOR, BmsCore::ResponseError::Terminator, F("bad terminator")},
        {CRC, BmsCore::ResponseError::Crc, F("corrupt CRC")}
    };

    for (const TestCase &test : cases) {
        uint8_t response[10] = {};
        makeValidResponse(response);
        size_t received = 9;
        switch (test.mutation) {
            case SHORT: received = 8; break;
            case LONG: received = 10; break;
            case ADDRESS: response[0] = 3; break;
            case FUNCTION: response[1] = 4; break;
            case BYTE_COUNT: response[2] = 4; break;
            case TERMINATOR: response[8] = 0; break;
            case CRC: response[5] ^= 0x01U; break;
            case VALID: break;
        }
        expectTrue(
            BmsCore::validateResponse(response, received, 9, 2, 2) == test.expected,
            test.name
        );
    }
}

void runDecodeTests() {
    expectTrue(
        BmsCore::decodeUnsigned16(0x12, 0x34) == 0x1234,
        F("unsigned 16-bit decode")
    );
    expectTrue(
        BmsCore::decodeSigned16(0xFF, 0x9C) == -100,
        F("signed 16-bit decode")
    );
    expectTrue(BmsCore::socTenthsPercent(0) == 0, F("SOC lower endpoint"));
    expectTrue(BmsCore::socTenthsPercent(128) == 502, F("SOC midpoint rounding"));
    expectTrue(BmsCore::socTenthsPercent(255) == 1000, F("SOC upper endpoint"));
}

void runThresholdTests() {
    expectTrue(BmsCore::assertHigh(3951, 3950), F("high alarm asserts above threshold"));
    expectTrue(!BmsCore::assertHigh(3950, 3950), F("high alarm excludes threshold"));
    expectTrue(BmsCore::clearHigh(3750, 3950, 200), F("high alarm clears at boundary"));
    expectTrue(!BmsCore::clearHigh(3751, 3950, 200), F("high alarm honors hysteresis"));
    expectTrue(BmsCore::assertLow(2599, 2600), F("low alarm asserts below threshold"));
    expectTrue(!BmsCore::assertLow(2600, 2600), F("low alarm excludes threshold"));
    expectTrue(BmsCore::clearLow(2800, 2600, 200), F("low alarm clears at boundary"));
    expectTrue(!BmsCore::clearLow(2799, 2600, 200), F("low alarm honors hysteresis"));
}

void runStorageTests() {
    const bool allValid[] = {true, true};
    const bool oneInvalid[] = {true, false};
    const uint16_t lowSoc[] = {400, 600};
    const uint16_t highSoc[] = {450, 500};

    BmsCore::StorageDecision decision =
        BmsCore::evaluateStorage(false, lowSoc, allValid, 2, 400, 500);
    expectTrue(decision.startCharging && !decision.stopCharging, F("storage starts at 40 percent"));

    decision = BmsCore::evaluateStorage(true, highSoc, allValid, 2, 400, 500);
    expectTrue(!decision.startCharging && decision.stopCharging, F("storage stops at 50 percent"));

    decision = BmsCore::evaluateStorage(true, highSoc, oneInvalid, 2, 400, 500);
    expectTrue(!decision.startCharging && !decision.stopCharging, F("invalid SOC holds storage state"));
}

void runIncompleteScanTests() {
    expectTrue(
        BmsCore::moduleScanComplete(true, true, true, true),
        F("four valid fields complete a scan")
    );
    expectTrue(
        !BmsCore::moduleScanComplete(true, true, false, true),
        F("one invalid field makes a scan incomplete")
    );

    BmsCore::CommunicationState state =
        BmsCore::evaluateCommunication(false, 0, false, 2);
    expectTrue(
        state.consecutiveFailures == 1 && state.warningLatched && !state.shutdown,
        F("first incomplete scan warns without shutdown")
    );
    state = BmsCore::evaluateCommunication(
        false,
        state.consecutiveFailures,
        state.warningLatched,
        2
    );
    expectTrue(
        state.consecutiveFailures == 2 && state.warningLatched && state.shutdown,
        F("second incomplete scan shuts down")
    );
    state = BmsCore::evaluateCommunication(
        true,
        state.consecutiveFailures,
        state.warningLatched,
        2
    );
    expectTrue(
        state.consecutiveFailures == 0 && state.warningLatched && !state.shutdown,
        F("complete scan clears shutdown but preserves warning latch")
    );
}

void runTimingTests() {
    expectTrue(
        BmsCore::intervalElapsed(5U, UINT32_MAX - 4U, 10U),
        F("millis rollover interval")
    );
    expectTrue(
        !BmsCore::intervalElapsed(5U, UINT32_MAX - 4U, 11U),
        F("millis rollover before interval")
    );
}

void setup() {
    Serial.begin(115200);
    delay(250);
    runCrcTests();
    runResponseValidationTests();
    runDecodeTests();
    runThresholdTests();
    runStorageTests();
    runIncompleteScanTests();
    runTimingTests();
    Serial.print(F("Failures: "));
    Serial.println(failures);
}

void loop() {}
