/* ST-3: 8-bit and 16-bit integer arithmetic paths. */
#include "abi_test.h"

volatile uint8_t u8a = 23, u8b = 7;
volatile uint16_t u16a = 1234, u16b = 37;
volatile int8_t s8a = -23, s8b = 7;
volatile int16_t s16a = -123, s16b = 37;

void main(void) {
    test_init();

    ASSERT((uint8_t)(u8a * u8b) == 161);
    ASSERT((uint8_t)(u8a / u8b) == 3);
    ASSERT((uint8_t)(u8a % u8b) == 2);
    ASSERT((uint8_t)(u8a << 2) == 92);
    ASSERT((uint8_t)(u8a >> 2) == 5);
    ASSERT(s8a * s8b == -161);
    ASSERT(s8a / s8b == -3);
    ASSERT(s8a % s8b == -2);

    ASSERT((uint16_t)(u16a * u16b) == 45658u);
    ASSERT((uint16_t)(u16a / u16b) == 33u);
    ASSERT((uint16_t)(u16a % u16b) == 13u);
    ASSERT((uint16_t)(u16a << 3) == 9872u);
    ASSERT((uint16_t)(u16a >> 3) == 154u);
    ASSERT(s16a * s16b == -4551);
    ASSERT(s16a / s16b == -3);
    ASSERT(s16a % s16b == -12);

    test_pass();
}
