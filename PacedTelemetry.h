#ifndef PACED_TELEMETRY_H
#define PACED_TELEMETRY_H

#include <Arduino.h>

// One immutable frame at a time. Formatting uses the existing Print interface;
// transmission is serviced by loop(), never by a blocking delay or flush.
template <size_t Capacity, size_t ChunkSize, uint32_t ChunkIntervalMs>
class PacedTelemetry : public Print {
public:
    static_assert(Capacity > 0 && ChunkSize > 0 && ChunkSize <= Capacity,
                  "Invalid telemetry buffer sizes");
    static_assert(ChunkIntervalMs > 0, "Telemetry pacing must be positive");
    using Print::write;

    bool beginFrame() {
        if (pending() || building_) {
            ++skippedFrames;
            return false;
        }
        length_ = 0;
        offset_ = 0;
        overflow_ = false;
        building_ = true;
        return true;
    }

    size_t write(uint8_t value) override {
        if (!building_ || overflow_) return 0;
        if (length_ == Capacity) {
            overflow_ = true;
            return 0;
        }
        bytes_[length_++] = value;
        return 1;
    }

    bool finishFrame() {
        if (!building_) return false;
        building_ = false;
        if (overflow_ || length_ == 0) {
            // Never transmit a truncated frame, even if its prefix fits.
            length_ = 0;
            ++rejectedFrames;
            return false;
        }
        ++queuedFrames;
        return true;
    }

    bool pending() const { return !building_ && offset_ < length_; }

    template <typename Port>
    void service(Port &port, uint32_t now) {
        if (!pending() || (hasSent_ &&
            static_cast<uint32_t>(now - lastChunkMs_) < ChunkIntervalMs)) return;
        const int available = port.availableForWrite();
        if (available <= 0) return;
        size_t count = length_ - offset_;
        if (count > ChunkSize) count = ChunkSize;
        if (count > static_cast<size_t>(available)) count = available;
        const size_t written = port.write(bytes_ + offset_, count);
        if (written == 0) return;
        offset_ += written;
        transmittedBytes += written;
        lastChunkMs_ = now;
        hasSent_ = true;
        if (!pending()) ++submittedFrames;
    }

    uint32_t queuedFrames = 0;
    uint32_t submittedFrames = 0;
    uint32_t skippedFrames = 0;
    uint32_t rejectedFrames = 0;
    uint32_t transmittedBytes = 0;

private:
    uint8_t bytes_[Capacity] = {};
    size_t length_ = 0;
    size_t offset_ = 0;
    uint32_t lastChunkMs_ = 0;
    bool building_ = false;
    bool overflow_ = false;
    bool hasSent_ = false;
};

#endif
