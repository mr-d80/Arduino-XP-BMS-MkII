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
        {0xFF, 0x00}, {0x3F, 0x00}, {0xFE, 0x01}, {0xFD, 0x02},
        {0x1D, 0x22}, {0xDD, 0x22}, {0x00, 0x3F},
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

    struct ActivityCase {
        bool statuswordValid;
        uint16_t rawStatusword;
        bool bankStateValid;
        uint8_t rawBankState;
        BmsCore::BalanceActivity expectedActivity;
        int8_t expectedTelemetryBal;
        int16_t expectedBankMask;
    };
    const ActivityCase activities[] = {
        // Captured rev. 2 module 8: statusword bit 8 and bank mask 0x22.
        {true, 0x0100, true, 0x1D, BmsCore::BalanceActivity::Active, 1, 0x22},
        // Captured rev. 1 module 18: bank mask 0x3E activates despite bit 8 being clear.
        {true, 0x0000, true, 0x01, BmsCore::BalanceActivity::Active, 1, 0x3E},
        // Six low flags all set means no active bank; do not confuse this with raw byte 0x00.
        {true, 0x0000, true, 0x3F, BmsCore::BalanceActivity::Inactive, 0, 0x00},
        {true, 0x0100, true, 0xFF, BmsCore::BalanceActivity::Active, 1, 0x00},
        // Upper bits are ignored: 0xDD has the same low six flags as 0x1D.
        {true, 0x0000, true, 0xDD, BmsCore::BalanceActivity::Active, 1, 0x22},
        {false, 0x0100, false, 0x1D, BmsCore::BalanceActivity::Unavailable, -1, -1},
        {false, 0x0100, true, 0xFF, BmsCore::BalanceActivity::Unavailable, -1, 0x00},
        {true, 0x0000, false, 0x01, BmsCore::BalanceActivity::Unavailable, -1, -1}
    };
    for (const ActivityCase &test : activities) {
        const BmsCore::BalanceActivity activity = BmsCore::inferBalanceActivity(
            test.statuswordValid,
            test.rawStatusword,
            test.bankStateValid,
            test.rawBankState
        );
        assert(activity == test.expectedActivity);
        if (activity == BmsCore::BalanceActivity::Unavailable) {
            assert(test.expectedTelemetryBal == -1);
        } else {
            const uint8_t telemetryBal = activity == BmsCore::BalanceActivity::Active ? 1U : 0U;
            assert(telemetryBal == static_cast<uint8_t>(test.expectedTelemetryBal));
        }
        if (test.bankStateValid) {
            assert(BmsCore::activeBalanceBankMask(test.rawBankState) ==
                   static_cast<uint8_t>(test.expectedBankMask));
        }
    }

    puts("PASS: strict IDs, raw status/bank diagnostics, combined activity, BAL mapping and validity gates");
}
