/* MT-4B gap: C statements in a naked body must not be silently discarded. */

void migration_naked_c_body(void) __naked
{
    unsigned char value;
    value = 1;
}
