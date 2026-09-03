/* MT-4B diagnostic: __interrupt (64) is outside the supported 0..48 envelope and must be rejected. */

void migration_invalid_interrupt(void) __interrupt (64)
{
}
