// Run the same assertions as the Teensy test sketch without serial hardware.
// Only its console/delay facade is mocked; BmsCore is the production header.
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

class __FlashStringHelper;
#define F(value) reinterpret_cast<const __FlashStringHelper *>(value)

struct TestConsole {
    void begin(unsigned long) {}
    void print(const __FlashStringHelper *value) {
        fputs(reinterpret_cast<const char *>(value), stdout);
    }
    void println(const __FlashStringHelper *value) {
        puts(reinterpret_cast<const char *>(value));
    }
    void println(uint16_t value) {
        printf("%u\n", static_cast<unsigned int>(value));
    }
};

TestConsole Serial;
void delay(unsigned long) {}

#include "../BmsCoreTests/BmsCoreTests.ino"

int main() {
    setup();
    return failures == 0 ? 0 : 1;
}
