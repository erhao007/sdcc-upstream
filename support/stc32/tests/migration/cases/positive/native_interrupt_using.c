/* Canonical combined ISR spelling; bank zero is deliberately conservative. */

volatile unsigned char migration_interrupt_using_ticks;

void migration_interrupt_using(void) __interrupt (1) __using (0)
{
    migration_interrupt_using_ticks++;
}
