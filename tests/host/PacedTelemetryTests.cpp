#include "../../PacedTelemetry.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>

struct Port {
    int available = 64;
    size_t writeLimit = 64;
    std::string output;
    std::vector<size_t> writes;
    int availableForWrite() { return available; }
    size_t write(const uint8_t *bytes, size_t size) {
        assert(size <= static_cast<size_t>(available));
        if (size > writeLimit) size = writeLimit;
        output.append(reinterpret_cast<const char *>(bytes), size);
        writes.push_back(size);
        return size;
    }
};

template <typename Queue>
void queueFrame(Queue &queue, const std::string &text) {
    assert(queue.beginFrame());
    assert(queue.write(reinterpret_cast<const uint8_t *>(text.data()), text.size()) == text.size());
    assert(queue.finishFrame());
}

int main() {
    {
        PacedTelemetry<2048, 32, 80> queue;
        Port port;
        std::string frame;
        for (int i = 0; i < 1800; ++i) frame += static_cast<char>('A' + i % 26);
        frame += "\r\n\r\n";
        queueFrame(queue, frame);
        // Model regular 83 ms scans: service may pause, but never catches up
        // with a burst of multiple chunks when it resumes.
        uint32_t lastWrite = 0;
        bool hasWritten = false;
        for (uint32_t now = 0; now < 12000; ++now) {
            if (now % 183 < 83) continue;
            const size_t before = port.output.size();
            queue.service(port, now);
            assert(port.output.size() - before <= 32);
            if (port.output.size() != before) {
                assert(!hasWritten || now - lastWrite >= 80);
                lastWrite = now;
                hasWritten = true;
            }
        }
        assert(port.output == frame);
        assert(queue.submittedFrames == 1 && !queue.pending());
        assert(queue.transmittedBytes == frame.size());
    }
    {
        PacedTelemetry<80, 32, 20> queue;
        Port port;
        queueFrame(queue, std::string(64, 'x'));
        queue.service(port, 100);
        assert(port.output.size() == 32);
        assert(!queue.beginFrame());
        assert(queue.write('y') == 0);
        queue.service(port, 119);
        assert(port.output.size() == 32);
        queue.service(port, 120);
        assert(port.output == std::string(64, 'x'));
        queueFrame(queue, "new\r\n\r\n");
        queue.service(port, 139);
        assert(port.output.size() == 64);
        queue.service(port, 140);
        assert(port.output == std::string(64, 'x') + "new\r\n\r\n");
        assert(queue.skippedFrames == 1 && queue.submittedFrames == 2);
    }
    {
        PacedTelemetry<8, 4, 20> queue;
        Port port;
        assert(queue.beginFrame());
        for (int i = 0; i < 9; ++i) queue.write('x');
        queue.service(port, 0); // Uncommitted output must never escape.
        assert(!queue.finishFrame());
        queue.service(port, 100);
        assert(port.output.empty() && queue.rejectedFrames == 1);
        queueFrame(queue, "12345678"); // Exact capacity is valid.
        queue.service(port, 100);
        queue.service(port, 120);
        assert(port.output == "12345678");
    }
    {
        PacedTelemetry<32, 4, 20> queue;
        Port port;
        queueFrame(queue, "abcdefgh");
        port.available = 0;
        queue.service(port, 100);
        assert(port.output.empty());
        port.available = 3;
        port.writeLimit = 0;
        queue.service(port, 100);
        assert(port.output.empty());
        port.writeLimit = 2;
        queue.service(port, UINT32_MAX - 9);
        assert(port.output == "ab");
        queue.service(port, 9);
        assert(port.output == "ab");
        queue.service(port, 10);
        assert(port.output == "abcd");
        queue.service(port, 30);
        queue.service(port, 50);
        assert(port.output == "abcdefgh" && queue.submittedFrames == 1);
    }
    puts("PASS: frame integrity, pacing, scan pauses, backpressure, partial writes, overflow, rollover");
}
