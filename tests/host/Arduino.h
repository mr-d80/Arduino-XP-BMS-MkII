#ifndef HOST_TEST_ARDUINO_H
#define HOST_TEST_ARDUINO_H

// Minimal Print contract for exercising the actual transmit queue on the host.
#include <stddef.h>
#include <stdint.h>
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t value) = 0;
    size_t write(const uint8_t *bytes, size_t size) {
        size_t written = 0;
        while (written < size && write(bytes[written]) == 1) ++written;
        return written;
    }
};

#endif
