/* Test-local, original mapping sketch; intentionally not a public header. */

#define MIGRATION_SFR(name, address) __sfr __at (address) name
#define MIGRATION_SBIT(name, address) __sbit __at (address) name

MIGRATION_SFR(migration_p0, 0x80);
MIGRATION_SBIT(migration_p0_0, 0x80);

void migration_sfr_sbit_probe(void)
{
    migration_p0 = 0xa5;
    migration_p0_0 = 1;
}
