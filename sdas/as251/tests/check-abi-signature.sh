#!/bin/sh

set -eu

if test "$#" -ne 2; then
    echo "usage: $0 /path/to/sdas251 /path/to/sdld" >&2
    exit 2
fi

absolute_program()
{
    case $1 in
        /*) printf '%s\n' "$1" ;;
        *)
            program_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
            printf '%s/%s\n' "$program_dir" "$(basename -- "$1")"
            ;;
    esac
}

assembler=$(absolute_program "$1")
linker=$(absolute_program "$2")
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")/abi" && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/sdas251-abi.XXXXXX")

cleanup()
{
    rm -rf "$work_dir"
}
trap cleanup 0 HUP INT TERM

cp "$test_dir/signature-a.asm" "$work_dir/"
cp "$test_dir/signature-b-same.asm" "$work_dir/"
cp "$test_dir/signature-b-mismatch.asm" "$work_dir/"
cp "$test_dir/signature-b-missing.asm" "$work_dir/"
cp "$test_dir/signature-b-large.asm" "$work_dir/"
cp "$test_dir/signature-b-major.asm" "$work_dir/"
cp "$test_dir/signature-b-unnamed-missing.asm" "$work_dir/"

signature='stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1'
mismatch_signature='stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=1 xstack=0 intlong-reent=1 float-reent=1 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1'
large_signature='stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=large stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1'
major_signature='stc32-mcs251 abi-major=0 abi-minor=3 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi0.3-r1'

(
    cd "$work_dir"
    "$assembler" -plosg signature-a.asm
    "$assembler" -plosg signature-b-same.asm
    "$assembler" -plosg signature-b-mismatch.asm
    "$assembler" -plosg signature-b-missing.asm
    "$assembler" -plosg signature-b-large.asm
    "$assembler" -plosg signature-b-major.asm
    "$assembler" -plosg signature-b-unnamed-missing.asm

    for object in signature-a.rel signature-b-same.rel; do
        actual=$(sed -n '/^O /{s/^O //;p;}' "$object")
        if test "$actual" != "$signature"; then
            echo "unexpected ABI signature in $object: $actual" >&2
            exit 1
        fi
    done

    actual=$(sed -n '/^O /{s/^O //;p;}' signature-b-mismatch.rel)
    if test "$actual" != "$mismatch_signature"; then
        echo "unexpected mismatch signature: $actual" >&2
        exit 1
    fi
    actual=$(sed -n '/^O /{s/^O //;p;}' signature-b-large.rel)
    if test "$actual" != "$large_signature"; then
        echo "unexpected large-model signature: $actual" >&2
        exit 1
    fi
    actual=$(sed -n '/^O /{s/^O //;p;}' signature-b-major.rel)
    if test "$actual" != "$major_signature"; then
        echo "unexpected legacy v0.3 signature: $actual" >&2
        exit 1
    fi
    if grep -q '^O ' signature-b-unnamed-missing.rel; then
        echo "unnamed negative fixture unexpectedly contains an ABI signature" >&2
        exit 1
    fi

    printf '%s\n' \
        '-mwx' \
        '-i linked-same.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' \
        'signature-b-same.rel' > same.lk

    printf '%s\n' \
        '-mwx' \
        '-i linked-mismatch.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' \
        'signature-b-mismatch.rel' > mismatch.lk

    "$linker" --mcs251-abi -nf same.lk > same.log 2>&1

    if "$linker" --mcs251-abi -nf mismatch.lk > mismatch.log 2>&1; then
        echo "ABI-mismatched objects unexpectedly linked" >&2
        cat mismatch.log >&2
        exit 1
    fi
    if ! grep -Fq '?ASlink-Error-MCS251 ABI mismatch:' mismatch.log; then
        echo "ABI mismatch did not produce the sdld conflict diagnostic" >&2
        cat mismatch.log >&2
        exit 1
    fi

    printf '%s\n' \
        '-mwx' \
        '-i linked-missing.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' \
        'signature-b-missing.rel' > missing.lk

    if "$linker" --mcs251-abi -nf missing.lk > missing.log 2>&1; then
        echo "ABI-missing object unexpectedly linked" >&2
        cat missing.log >&2
        exit 1
    fi
    if ! grep -Fq '?ASlink-Error-MCS251 ABI signature missing in module' missing.log; then
        echo "missing ABI signature did not produce the fail-closed diagnostic" >&2
        cat missing.log >&2
        exit 1
    fi

    printf '%s\n' \
        '-mwx' \
        '-i linked-unnamed-missing.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-b-unnamed-missing.rel' > unnamed-missing.lk

    if "$linker" --mcs251-abi -nf unnamed-missing.lk > unnamed-missing.log 2>&1; then
        echo "ABI-missing unnamed module unexpectedly linked" >&2
        cat unnamed-missing.log >&2
        exit 1
    fi
    if ! grep -Fq '?ASlink-Error-MCS251 ABI signature missing in module "' unnamed-missing.log; then
        echo "unnamed ABI signature did not produce the fail-closed diagnostic" >&2
        cat unnamed-missing.log >&2
        exit 1
    fi
    if ! grep -Fq 'signature-b-unnamed-missing.rel' unnamed-missing.log; then
        echo "unnamed ABI signature diagnostic did not identify the input file" >&2
        cat unnamed-missing.log >&2
        exit 1
    fi
    if test -e linked-unnamed-missing.ihx; then
        echo "ABI-missing unnamed module produced linked output" >&2
        exit 1
    fi

    printf '%s\n' \
        '-mwx' \
        '-i linked-large-mismatch.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' \
        'signature-b-large.rel' > large-mismatch.lk

    if "$linker" --mcs251-abi -nf large-mismatch.lk > large-mismatch.log 2>&1; then
        echo "ABI memory-model mismatch unexpectedly linked" >&2
        cat large-mismatch.log >&2
        exit 1
    fi
    if ! grep -Fq '?ASlink-Error-MCS251 ABI mismatch:' large-mismatch.log; then
        echo "ABI memory-model mismatch did not produce the sdld conflict diagnostic" >&2
        cat large-mismatch.log >&2
        exit 1
    fi

    # A single internally consistent object must still match the driver-owned
    # expectation.  These cases fail with the old first-object baseline.
    printf '%s\n' \
        '-mwx' \
        '-i linked-expected-large.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-b-large.rel' > expected-large.lk

    if "$linker" --mcs251-abi -nf expected-large.lk > expected-large.log 2>&1; then
        echo "large object unexpectedly overrode the driver ABI expectation" >&2
        cat expected-large.log >&2
        exit 1
    fi
    grep -Fq '?ASlink-Error-MCS251 ABI mismatch:' expected-large.log

    # The strict check must not depend on where -A appears in a hand-written
    # link command file.  Keep the object first to exercise deferred checking.
    printf '%s\n' \
        '-mwx' \
        '-i linked-late-expectation.ihx' \
        '-b MCS251CODE = 0xff0000' \
        'signature-b-large.rel' \
        "-A $signature" > late-expectation.lk

    if "$linker" --mcs251-abi -nf late-expectation.lk > late-expectation.log 2>&1; then
        echo "late driver ABI expectation unexpectedly accepted a mismatched object" >&2
        cat late-expectation.log >&2
        exit 1
    fi
    grep -Fq '?ASlink-Error-MCS251 ABI mismatch:' late-expectation.log
    if test -e linked-late-expectation.ihx; then
        echo "late driver ABI expectation produced linked output" >&2
        exit 1
    fi

    printf '%s\n' \
        '-mwx' \
        '-i linked-expected-stack-auto.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-b-mismatch.rel' > expected-stack-auto.lk

    if "$linker" --mcs251-abi -nf expected-stack-auto.lk > expected-stack-auto.log 2>&1; then
        echo "stack-auto object unexpectedly overrode the driver ABI expectation" >&2
        cat expected-stack-auto.log >&2
        exit 1
    fi
    grep -Fq '?ASlink-Error-MCS251 ABI mismatch:' expected-stack-auto.log

    printf '%s\n' \
        '-mwx' \
        '-i linked-no-expectation.ihx' \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' > no-expectation.lk

    if "$linker" --mcs251-abi -nf no-expectation.lk > no-expectation.log 2>&1; then
        echo "strict ABI link unexpectedly accepted no driver expectation" >&2
        cat no-expectation.log >&2
        exit 1
    fi
    grep -Fq '?ASlink-Error-MCS251 ABI expected signature was not provided' no-expectation.log

    printf '%s\n' \
        '-mwx' \
        '-i linked-major-mismatch.ihx' \
        "-A $signature" \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' \
        'signature-b-major.rel' > major-mismatch.lk

    if "$linker" --mcs251-abi -nf major-mismatch.lk > major-mismatch.log 2>&1; then
        echo "legacy ABI v0.3 object unexpectedly linked" >&2
        cat major-mismatch.log >&2
        exit 1
    fi
    if ! grep -Fq '?ASlink-Error-MCS251 ABI signature rejected in module' major-mismatch.log; then
        echo "legacy ABI v0.3 object did not produce the signature rejection diagnostic" >&2
        cat major-mismatch.log >&2
        exit 1
    fi
)

echo "ABI 1.0 signature transport: driver expectation enforced; same signature accepted; forced-option, model, legacy-v0.3, named-missing and unnamed-missing cases rejected"
