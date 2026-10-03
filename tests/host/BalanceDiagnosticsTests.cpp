#include "../../BmsCore.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct IdCase {
    const char *text;
    bool valid;
    uint8_t expectedId;
};

struct StatusCase {
    uint16_t raw;
    bool generalActive;
};

struct BankCase {
    uint8_t raw;
    uint8_t activeMask;
};

struct EnableCase {
    uint16_t raw;
    bool enabled;
};

int main() {
    const IdCase ids[] = {
        {"1", true, 1}, {"48", true, 48}, {"01", true, 1},
        {"", false, 0}, {"0", false, 0}, {"49", false, 0},
        {"+1", false, 0}, {"1x", false, 0}, {" 1", false, 0}
    };
    for (const IdCase &test : ids) {
        uint8_t id = 0;
        const bool valid = BmsCore::parseBalanceDiagnosticModuleId(test.text, id);
        assert(valid == test.valid);
        if (valid) assert(id == test.expectedId);
    }

    const StatusCase statuses[] = {
        {0x0000, false}, {0x0001, false}, {0x0100, true},
        {0x0200, false}, {0x8100, true}
    };
    for (const StatusCase &test : statuses) {
        assert(BmsCore::balanceGeneralActive(test.raw) == test.generalActive);
    }

    const BankCase banks[] = {
        {0xFF, 0x00}, {0xFE, 0x01}, {0xFD, 0x02},
        {0xC0, 0x3F}, {0x80, 0x3F}, {0xBF, 0x00}
    };
    for (const BankCase &test : banks) {
        assert(BmsCore::activeBalanceBankMask(test.raw) == test.activeMask);
    }

    const EnableCase enableWords[] = {
        {0x0000, true}, {0x0001, true}, {0x0010, false},
        {0x0110, false}, {0x0020, true}
    };
    for (const EnableCase &test : enableWords) {
        assert(BmsCore::oemBalanceEnabled(test.raw) == test.enabled);
    }

    assert(BmsCore::inferBalanceActivity(false, 0x0100, true, 0xFF) ==
           BmsCore::BalanceActivity::Unavailable);
    assert(BmsCore::inferBalanceActivity(true, 0x0000, false, 0xFE) ==
           BmsCore::BalanceActivity::Unavailable);
    assert(BmsCore::inferBalanceActivity(true, 0x0100, true, 0xFF) ==
           BmsCore::BalanceActivity::Active);
    assert(BmsCore::inferBalanceActivity(true, 0x0000, true, 0xFE) ==
           BmsCore::BalanceActivity::Active);
    assert(BmsCore::inferBalanceActivity(true, 0x0000, true, 0xFF) ==
           BmsCore::BalanceActivity::Inactive);

    puts("PASS: strict IDs, active-low bank mask, general activity, enable bit, validity gates");
}
