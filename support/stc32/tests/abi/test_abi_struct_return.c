/* test_abi_struct_return.c - Test struct pass-by-value and aggregate return via hidden destination pointer */
#include "abi_test.h"

typedef struct {
    uint8_t a;
    uint8_t b;
} Point8;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint32_t z;
} Packet;

typedef struct {
    /* 9 bytes: still a >8-byte aggregate (hidden-pointer return path),
       while leaving enough direct RAM above the 0x30..0x37 test control
       block in model-small. */
    uint8_t bytes[9];
} LargeBlock;

Point8 make_point(uint8_t a, uint8_t b) {
    Point8 p;
    p.a = a;
    p.b = b;
    return p;
}

Point8 pass_small_struct_by_value(Point8 p, uint8_t offset) {
    Point8 res;
    res.a = p.a + offset;
    res.b = p.b + offset;
    return res;
}

Packet process_packet(const Packet *in, uint16_t delta) {
    Packet out;
    out.x = in->x + delta;
    out.y = in->y + delta;
    out.z = in->z + (uint32_t)delta;
    return out;
}

LargeBlock fill_block(uint8_t seed) {
    LargeBlock b;
    for (uint8_t i = 0; i < 9; ++i) {
        b.bytes[i] = seed + i;
    }
    return b;
}

void main(void) {
    test_init();

    /* 1. Small struct return */
    Point8 pt = make_point(0x12, 0x34);
    ASSERT(pt.a == 0x12);
    ASSERT(pt.b == 0x34);

    /* 2. Small struct pass-by-value */
    Point8 pt2 = pass_small_struct_by_value(pt, 0x10);
    ASSERT(pt2.a == 0x22);
    ASSERT(pt2.b == 0x44);

    /* 3. Medium struct return */
    Packet pin;
    pin.x = 100;
    pin.y = 200;
    pin.z = 30000;
    Packet pout = process_packet(&pin, 50);
    ASSERT(pout.x == 150);
    ASSERT(pout.y == 250);
    ASSERT(pout.z == 30050);

    /* 4. Large struct return via hidden pointer */
    LargeBlock blk = fill_block(0x40);
    for (uint8_t i = 0; i < 9; ++i) {
        ASSERT(blk.bytes[i] == (uint8_t)(0x40 + i));
    }

    test_pass();
}
