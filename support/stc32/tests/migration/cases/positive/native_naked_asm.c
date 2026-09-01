/* A naked body is only meaningful when its instructions and return are explicit. */

void migration_naked_asm(void) __naked
{
    __asm
        nop
        ret
    __endasm;
}
