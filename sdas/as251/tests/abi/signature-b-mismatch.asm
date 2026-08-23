        .module abi_signature_b_mismatch
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=1 xstack=0 intlong-reent=1 float-reent=1 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
        .globl abi_signature_target
        .area MCS251CODE (REL,CON,CODE)

abi_signature_target::
        nop
