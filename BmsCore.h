#ifndef XP_BMS_CORE_H
#define XP_BMS_CORE_H

#include <stddef.h>
#include <stdint.h>

namespace BmsCore {

enum class ResponseError : uint8_t {
    None,
    TooShort,
    TooLong,
    Address,
    Function,
    ByteCount,
    Terminator,
    Crc
};

struct StorageDecision {
    bool startCharging;
    bool stopCharging;
};

struct CommunicationState {
    uint8_t consecutiveFailures;
    bool warningLatched;
    bool shutdown;
};

inline uint16_t modbusCrc(const uint8_t *buffer, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t position = 0; position < length; ++position) {
        crc ^= static_cast<uint16_t>(buffer[position]);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

inline uint16_t decodeUnsigned16(uint8_t highByte, uint8_t lowByte) {
    return static_cast<uint16_t>((static_cast<uint16_t>(highByte) << 8U) | lowByte);
}

inline ResponseError validateResponse(
    const uint8_t *response,
    size_t receivedLength,
    size_t expectedLength,
    uint8_t expectedAddress,
    uint8_t expectedByteCount
) {
    if (receivedLength < expectedLength || response == nullptr || expectedLength < 7U) {
        return ResponseError::TooShort;
    }
    if (receivedLength > expectedLength) {
        return ResponseError::TooLong;
    }
    if (response[0] != expectedAddress) {
        return ResponseError::Address;
    }
    if (response[1] != 0x03U) {
        return ResponseError::Function;
    }
    if (
        response[2] != expectedByteCount ||
        expectedLength != static_cast<size_t>(expectedByteCount) + 7U
    ) {
        return ResponseError::ByteCount;
    }
    if (response[expectedLength - 2U] != 0x0DU || response[expectedLength - 1U] != 0x0AU) {
        return ResponseError::Terminator;
    }

    const size_t crcPosition = expectedLength - 4U;
    const uint16_t crc = modbusCrc(response, crcPosition);
    if (
        response[crcPosition] != static_cast<uint8_t>(crc & 0x00FFU) ||
        response[crcPosition + 1U] != static_cast<uint8_t>(crc >> 8U)
    ) {
        return ResponseError::Crc;
    }
    return ResponseError::None;
}

inline int16_t decodeSigned16(uint8_t highByte, uint8_t lowByte) {
    return static_cast<int16_t>(decodeUnsigned16(highByte, lowByte));
}

inline uint16_t socTenthsPercent(uint8_t rawSoc) {
    // Round the 0..255 register to 0.0..100.0 percent.
    return static_cast<uint16_t>((static_cast<uint32_t>(rawSoc) * 1000U + 127U) / 255U);
}

inline bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval) {
    return static_cast<uint32_t>(now - previous) >= interval;
}

inline bool moduleScanComplete(
    bool voltageValid,
    bool temperatureValid,
    bool socCurrentValid,
    bool balanceValid
) {
    return voltageValid && temperatureValid && socCurrentValid && balanceValid;
}

inline CommunicationState evaluateCommunication(
    bool completeScan,
    uint8_t currentFailures,
    bool warningLatched,
    uint8_t maximumConsecutiveFailures
) {
    if (completeScan) {
        return {0, warningLatched, false};
    }

    const uint8_t failures = currentFailures == UINT8_MAX
        ? UINT8_MAX
        : static_cast<uint8_t>(currentFailures + 1U);
    return {
        failures,
        true,
        failures >= maximumConsecutiveFailures
    };
}

inline bool assertHigh(int32_t value, int32_t threshold) {
    return value > threshold;
}

inline bool clearHigh(int32_t maximumValue, int32_t threshold, int32_t hysteresis) {
    return maximumValue <= threshold - hysteresis;
}

inline bool assertLow(int32_t value, int32_t threshold) {
    return value < threshold;
}

inline bool clearLow(int32_t minimumValue, int32_t threshold, int32_t hysteresis) {
    return minimumValue >= threshold + hysteresis;
}

inline StorageDecision evaluateStorage(
    bool charging,
    const uint16_t *socTenths,
    const bool *valid,
    size_t moduleCount,
    uint16_t minimumSocTenths,
    uint16_t maximumSocTenths
) {
    bool reachedMinimum = false;
    bool reachedMaximum = false;

    for (size_t index = 0; index < moduleCount; ++index) {
        if (!valid[index]) {
            return {false, false};
        }
        if (socTenths[index] <= minimumSocTenths) {
            reachedMinimum = true;
        }
        if (socTenths[index] >= maximumSocTenths) {
            reachedMaximum = true;
        }
    }

    StorageDecision decision = {false, false};
    decision.startCharging = !charging && reachedMinimum;
    decision.stopCharging = charging && reachedMaximum && !reachedMinimum;
    return decision;
}

}  // namespace BmsCore

#endif
