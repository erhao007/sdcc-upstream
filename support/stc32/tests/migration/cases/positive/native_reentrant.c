/* Canonical reentrant spelling; stack behavior is covered by the ABI runner. */

unsigned char migration_reentrant(unsigned char value) __reentrant
{
    return (unsigned char)(value + 1);
}
