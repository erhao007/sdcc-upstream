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

signature='stc32-mcs251 abi-major=0 abi-minor=3 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi0.3-r1'
mismatch_signature='stc32-mcs251 abi-major=0 abi-minor=3 target=mcs251 model=small stack-auto=1 xstack=0 intlong-reent=1 float-reent=1 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi0.3-r1'

(
    cd "$work_dir"
    "$assembler" -plosg signature-a.asm
    "$assembler" -plosg signature-b-same.asm
    "$assembler" -plosg signature-b-mismatch.asm

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

    printf '%s\n' \
        '-mwx' \
        '-i linked-same.ihx' \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' \
        'signature-b-same.rel' > same.lk

    printf '%s\n' \
        '-mwx' \
        '-i linked-mismatch.ihx' \
        '-b MCS251CODE = 0xff0000' \
        'signature-a.rel' \
        'signature-b-mismatch.rel' > mismatch.lk

    "$linker" -nf same.lk > same.log 2>&1

    if "$linker" -nf mismatch.lk > mismatch.log 2>&1; then
        echo "ABI-mismatched objects unexpectedly linked" >&2
        cat mismatch.log >&2
        exit 1
    fi
    if ! grep -Fq '?ASlink-Warning-Conflicting sdcc options:' mismatch.log; then
        echo "ABI mismatch did not produce the sdld conflict diagnostic" >&2
        cat mismatch.log >&2
        exit 1
    fi
)

echo "ABI signature transport: same signature accepted; mismatch rejected"
