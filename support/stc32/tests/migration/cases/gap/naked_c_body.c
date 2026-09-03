/* MT-4B diagnostic: C statements in a __naked body must be rejected, not discarded. */

void migration_naked_c_body(void) __naked
{
    unsigned char value;
    value = 1;
}
