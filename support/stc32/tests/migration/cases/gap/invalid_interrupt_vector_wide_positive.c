/* MT-4B diagnostic: a wide positive vector must not wrap into the valid range. */
void migration_invalid_interrupt_wide_positive(void) __interrupt (4294967296)
{
}
