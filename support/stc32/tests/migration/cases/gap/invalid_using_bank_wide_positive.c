/* MT-4B diagnostic: a wide positive bank must not wrap into the valid range. */
void migration_invalid_using_wide_positive(void) __using (4294967296)
{
}
