        .module t251
        .area MCS251TEST (ABS,CODE)
        .org 0
        ; MCS-251 Source-mode disassembler coverage test.
        ; Assembled by sdas251 (defaults to source mode).  The checked-in
        ; test-251.ihx is the byte image of this program; regenerate via:
        ;   sdas251 -plosg test-251.asm   # produces test-251.lst
        ;   (then scrape .lst hex columns -> test-251.ihx, Intel HEX)

        ; --- 8051-shared single/double-byte opcodes ---
        nop
        mov a,#0x10
        mov 0x30,#0x55
        mov a,0x82
        mov 0xa8,a
        add a,#5
        add a,0x30
        anl a,#0xf
        orl 0x90,a
        inc a
        inc 0x30
        dec a
        rr a
        rlc a
        swap a
        clr a
        clr c
        setb c
        lcall sub
        ljmp over
sub:    ret
over:

        ; --- A5 prefix (register-file ops via the inherited 8051 encodings) ---
        mov r4,a
        mov a,r5
        add a,r6
        add a,@r0
        inc @r1
        mov @r0,#0x99
        xch a,r3
        cjne r2,#0x7,br1
br1:
        djnz r4,br2
br2:

        ; --- 7E prefix (MOV immediate/direct family) ---
        mov r3,#0xaa
        mov r3,0x40
        mov wr6,#0x1234
        mov wr6,0x82
        mov dr8,#0x4000

        ; --- 7C/7D/7F register-to-register moves ---
        mov wr2,wr6
        mov dr8,dr12

        ; --- 0B/1B INC/DEC family ---
        inc wr6
        inc dr8
        dec wr2

        ; --- register-register ALU (2C/9C/.. and 2D/2F/..) ---
        add r11,r15
        sub r3,r4
        add wr4,wr6
        add dr8,dr12

        ; --- 24-bit control flow + MUL/DIV/CMP ---
        ejmp far
        ecall esub
esub:   eret
far:
        mul ab
        div ab
        cmp r3,r0

        ; --- CA/DA PUSH/POP register family + classic PUSH/POP dir8 ---
        push 0xa8
        push r4
        push wr2
        pop dr4

        ; --- relative branch (label -> assembler computes rel) ---
        sjmp done
done:   ret
