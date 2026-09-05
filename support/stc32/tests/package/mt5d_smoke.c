/* MT-5D clean-room package smoke: no vendor source or device header. */
#include <stdint.h>

volatile __data uint8_t mt5d_marker;

void main(void)
{
    mt5d_marker = 0x5a;
    for (;;)
        ;
}
