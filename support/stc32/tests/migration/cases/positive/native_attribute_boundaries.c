/* The upper edge of the STC32 Source Mode attribute envelope is supported. */

volatile unsigned char migration_boundary_ticks;

void migration_attribute_boundaries(void) __interrupt (48) __using (3)
{
    migration_boundary_ticks++;
}
