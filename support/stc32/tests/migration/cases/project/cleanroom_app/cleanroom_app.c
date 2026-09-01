/*
 * Independently written project-style sample.  It models a small data capture
 * module without using any vendor header, peripheral definition or startup.
 */

#include "cleanroom_app.h"

static __data unsigned char cleanroom_count;
static __idata unsigned char cleanroom_tag_seed;
static __xdata unsigned char cleanroom_checksum;
static const __code unsigned char cleanroom_weights[4] = {1, 3, 5, 7};
static __bit cleanroom_ready;

void cleanroom_reset(void)
{
    cleanroom_count = 0;
    cleanroom_tag_seed = 0x5a;
    cleanroom_checksum = 0;
    cleanroom_ready = 0;
}

unsigned int cleanroom_accumulate(const unsigned char *input,
                                  unsigned char count) __reentrant
{
    unsigned char index;
    unsigned int total = 0;

    if (count > 8)
        count = 8;
    for (index = 0; index < count; ++index)
    {
        unsigned char sample = input[index];

        total += (unsigned int)sample * cleanroom_weights[index & 3];
    }
    cleanroom_count = count;
    cleanroom_checksum = (unsigned char)total;
    cleanroom_ready = count != 0;
    return total;
}

unsigned char cleanroom_snapshot(unsigned char index)
{
    if (!cleanroom_ready || index >= cleanroom_count)
        return 0xff;
    return cleanroom_checksum ^ cleanroom_tag_seed ^ cleanroom_count ^ index;
}
