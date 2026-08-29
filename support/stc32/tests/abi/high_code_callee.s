; high_code_callee.s - Callee located in Region 1 (0x010900) for 24-bit indirect call testing

        .module high_code_callee
        .globl  _high_code_target

        .area   MCS251REG1 (ABS)
        .org    0x010900

_high_code_target:
        ; Input in DPH:DPL = arg. Return (arg + 0x0700) in DPH:DPL.
        mov     a, dph
        add     a, #0x07
        mov     dph, a
        eret
