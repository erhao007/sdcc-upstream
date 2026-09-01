/* Valid vector syntax only; device-specific vector ownership is out of scope. */

volatile unsigned char migration_interrupt_ticks;

void migration_interrupt(void) __interrupt (1)
{
    migration_interrupt_ticks++;
}
