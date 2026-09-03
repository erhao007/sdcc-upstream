/* MT-4B diagnostic: a wide negative vector must not wrap into the valid range. */
void migration_invalid_interrupt_wide_negative(void) __interrupt (-4294967296)
{
}
