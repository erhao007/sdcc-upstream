/* MT-4B diagnostic: a wide negative bank must not wrap into the valid range. */
void migration_invalid_using_wide_negative(void) __using (-4294967296)
{
}
