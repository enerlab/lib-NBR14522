#include "doctest/doctest.h"
#include <CRC.h>
#include <array>
#include <types_local.h>

// https://crccalc.com/ (CRC-16/ARC)

// lib python crccheck
// from crccheck.crc import CrcArc
// CrcArc.calc([arr])

TEST_CASE("CRC16") {

    uint16_t crc;

    byte_t data[] = {
        32, 50, 96, 7,   96, 34, 21, 0,  7,  16,  33,  5,  34, 21, 0,  7,
        16, 33, 34, 21,  0,  7,  16, 33, 0,  0,   0,   1,  16, 33, 0,  0,
        0,  0,  0,  0,   0,  0,  0,  0,  0,  0,   0,   0,  0,  0,  0,  0,
        0,  0,  24, 0,   24, 0,  24, 0,  24, 0,   33,  0,  33, 0,  33, 0,
        33, 0,  0,  0,   0,  0,  0,  0,  0,  0,   0,   0,  0,  0,  89, 133,
        1,  21, 21, 18,  16, 33, 18, 16, 33, 2,   17,  33, 21, 17, 33, 2,
        17, 33, 21, 17,  33, 37, 18, 33, 37, 18,  33,  1,  1,  34, 1,  1,
        34, 1,  3,  34,  21, 4,  34, 33, 4,  34,  33,  4,  34, 1,  5,  34,
        0,  0,  1,  0,   0,  1,  0,  0,  1,  0,   0,   1,  0,  0,  1,  0,
        0,  1,  1,  113, 49, 0,  0,  0,  22, 129, 0,   1,  1,  0,  0,  0,
        0,  0,  0,  0,   0,  0,  0,  0,  0,  0,   0,   0,  0,  0,  0,  0,
        0,  0,  0,  0,   0,  0,  0,  0,  0,  0,   0,   0,  0,  0,  0,  0,
        0,  0,  0,  1,   16, 17, 82, 1,  0,  0,   0,   5,  0,  0,  0,  0,
        2,  0,  0,  0,   0,  0,  96, 0,  96, 146, 146, 6,  0,  6,  0,  0,
        0,  0,  0,  6,   0,  6,  0,  0,  0,  0,   0,   51, 51, 3,  0,  1,
        1,  0,  0,  0,   0,  1,  7,  0,  0,  0,   0,   0,  0,  0,  0,  0};
    crc = CRC16(data, sizeof(data));
    CHECK(crc == 0x5C48);

    byte_t data0[] = {0x11, 0x22, 0x33, 0x44};
    crc = CRC16(data0, sizeof(data0));
    CHECK(crc == 0xF5B1);
}
