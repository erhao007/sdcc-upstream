/* Original MT-4A fixture: exercise SDCC's canonical address-space forms. */

__data unsigned char migration_data;
__idata unsigned char migration_idata;
__xdata unsigned char migration_xdata;
const __code unsigned char migration_code[] = {0xa5, 0x5a};
__bit migration_bit;

__sfr __at (0x80) migration_sfr;
__sbit __at (0x80) migration_sbit;

unsigned char migration_qualifier_probe(void)
{
    migration_data = 0x11;
    migration_idata = 0x22;
    migration_xdata = 0x33;
    migration_bit = 1;
    migration_sfr = 0x44;
    migration_sbit = 1;
    return migration_data + migration_idata + migration_xdata +
           migration_code[0] + migration_bit + migration_sfr + migration_sbit;
}
