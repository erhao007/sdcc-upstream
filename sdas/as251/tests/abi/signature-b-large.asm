        .module abi_signature_b_large
        .optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=large stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
        .globl abi_signature_target
        .area MCS251CODE (REL,CON,CODE)

abi_signature_target::
        nop
