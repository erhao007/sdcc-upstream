#!/bin/sh
set -eu

if test "$#" -lt 4; then
    echo "usage: $0 SDCC SMOKE_SOURCE DEVICE_INCLUDE RUNTIME_SOURCE..." >&2
    exit 2
fi

sdcc=$1
smoke_source=$2
device_include=$3
shift 3
test_dir=${TMPDIR:-/tmp}/sdcc-mcs251-driver.$$
trap 'rm -rf "$test_dir"' 0 HUP INT TERM
mkdir -p "$test_dir"

"$sdcc" -mmcs251 -I"$device_include" -S \
    -o "$test_dir/compiler-smoke.asm" "$smoke_source"
test -s "$test_dir/compiler-smoke.asm"
grep -Eq '^[[:space:]]*\.optsdcc stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 ' \
    "$test_dir/compiler-smoke.asm"

"$sdcc" -mstc32 -I"$device_include" -S \
    -o "$test_dir/compiler-stc32-alias.asm" "$smoke_source"
test -s "$test_dir/compiler-stc32-alias.asm"
mmcs251_signature=$(sed -n 's/^[[:space:]]*\.optsdcc //p' \
    "$test_dir/compiler-smoke.asm")
stc32_signature=$(sed -n 's/^[[:space:]]*\.optsdcc //p' \
    "$test_dir/compiler-stc32-alias.asm")
test "$mmcs251_signature" = "$stc32_signature"
test "$mmcs251_signature" = \
    'stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1'

# Each global option represented by the ABI contract must change the emitted
# identity deterministically.  Linker mismatch rejection is covered by the
# sdas251 ABI matrix; this table checks the compiler side of that boundary.
signature_case=0
while IFS='|' read -r flags expected; do
    test -n "$flags" || continue
    signature_case=$((signature_case + 1))
    # Intentional word splitting: every table entry is a list of SDCC flags.
    # shellcheck disable=SC2086
    "$sdcc" -mmcs251 $flags -I"$device_include" -S \
        -o "$test_dir/compiler-signature-$signature_case.asm" "$smoke_source"
    actual=$(sed -n 's/^[[:space:]]*\.optsdcc //p' \
        "$test_dir/compiler-signature-$signature_case.asm")
    if test "$actual" != "$expected"; then
        echo "unexpected MCS251 ABI signature for flags: $flags" >&2
        echo "expected: $expected" >&2
        echo "actual:   $actual" >&2
        exit 1
    fi
done <<'EOF'
--model-large|stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=large stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
--stack-auto|stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=1 xstack=0 intlong-reent=1 float-reent=1 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
--xstack|stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=1 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
--int-long-reent|stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=1 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
--float-reent|stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=1 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
--no-reg-params|stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=0 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
--all-callee-saves|stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=1 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
EOF

"$sdcc" -mmcs251 -I"$device_include" -c \
    -o "$test_dir/compiler-smoke.rel" "$smoke_source"
test -s "$test_dir/compiler-smoke.rel"

if "$sdcc" -mmcs251 --no-optsdcc-in-asm -I"$device_include" \
    -o "$test_dir/compiler-unsigned.ihx" "$smoke_source" \
    >"$test_dir/compiler-unsigned.log" 2>&1; then
    echo "unsigned MCS251 compiler output unexpectedly linked" >&2
    exit 1
fi
grep -Fq '?ASlink-Error-MCS251 ABI signature missing in module' \
    "$test_dir/compiler-unsigned.log"
test ! -e "$test_dir/compiler-unsigned.ihx"

# The strict link must use the ABI selected by this driver invocation.  A
# consistent set of prebuilt objects may not establish its own baseline and
# override model, stack or calling-option flags chosen for the current link.
while IFS='|' read -r case_name object_flags driver_flags; do
    test -n "$case_name" || continue
    # Intentional word splitting: the table fields are SDCC option lists.
    # shellcheck disable=SC2086
    "$sdcc" -mmcs251 $object_flags -I"$device_include" -c \
        -o "$test_dir/driver-expect-$case_name.rel" "$smoke_source"
    # shellcheck disable=SC2086
    if "$sdcc" -mmcs251 $driver_flags --nostdlib \
        -o "$test_dir/driver-expect-$case_name.ihx" \
        "$test_dir/driver-expect-$case_name.rel" \
        >"$test_dir/driver-expect-$case_name.log" 2>&1; then
        echo "MCS251 driver expectation mismatch unexpectedly linked: $case_name" >&2
        exit 1
    fi
    grep -Fq '?ASlink-Error-MCS251 ABI mismatch:' \
        "$test_dir/driver-expect-$case_name.log"
    test ! -e "$test_dir/driver-expect-$case_name.ihx"
done <<'EOF'
model-large|--model-large|--model-small
stack-auto|--stack-auto|--model-small
no-reg-params|--no-reg-params|--model-small
EOF

runtime_index=0
for runtime_source in "$@"; do
    for configuration in small small-stack-auto large large-stack-auto; do
        model=${configuration%%-*}
        stack_flag=
        case $configuration in
            *-stack-auto) stack_flag=--stack-auto ;;
        esac
        stack_value=0
        rent_value=0
        if test -n "$stack_flag"; then
            stack_value=1
            rent_value=1
        fi
        runtime_output=$test_dir/runtime-$runtime_index-$configuration.rel
        runtime_asm=$test_dir/runtime-$runtime_index-$configuration.asm
        "$sdcc" -mmcs251 --model-$model $stack_flag \
            -I"$device_include" -I"$device_include/mcs51" -S \
            -o "$runtime_asm" "$runtime_source"
        if grep -Eq '^[[:space:]]*(lcall|ret)([[:space:];]|$)' "$runtime_asm"; then
            echo "MCS251 runtime emitted a near call or return: $runtime_source ($configuration)" >&2
            exit 1
        fi
        runtime_signature=$(sed -n 's/^[[:space:]]*\.optsdcc //p' \
            "$runtime_asm")
        expected_runtime_signature="stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=$model stack-auto=$stack_value xstack=0 intlong-reent=$rent_value float-reent=$rent_value reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1"
        test "$runtime_signature" = "$expected_runtime_signature"
        case $runtime_source in
            */_setjmp.c|_setjmp.c)
                grep -q '^___mcs251_longjmp_restore:' "$runtime_asm"
                grep -q 'mov[[:space:]][[:space:]]*spx,dpx' "$runtime_asm"
                ;;
        esac
        "$sdcc" -mmcs251 --model-$model $stack_flag \
            -I"$device_include" -I"$device_include/mcs51" -c \
            -o "$runtime_output" "$runtime_source"
        test -s "$runtime_output"
    done
    runtime_index=$((runtime_index + 1))
done
