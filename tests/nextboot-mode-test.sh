#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The hook only defines functions when sourced; parsing a file does not touch
# block devices and is safe to exercise on the build host.
info() { :; }
. "$root/initramfs/20-recovery"

assert_mode() {
    expected=$1
    file=$2
    actual=$(recovery_parse_nextboot_mode_file "$file")
    [ "$actual" = "$expected" ] || {
        echo "expected mode '$expected', got '$actual'" >&2
        exit 1
    }
}

printf 'recovery\n' >"$work/lf"
assert_mode recovery "$work/lf"

printf 'recovery\r\n' >"$work/crlf"
assert_mode recovery "$work/crlf"

printf 'factory-reset\r\nignored\r\n' >"$work/factory-crlf"
assert_mode factory-reset "$work/factory-crlf"

printf 'unknown\r\n' >"$work/unknown"
assert_mode '' "$work/unknown"

assert_mode '' "$work/missing"

echo "next boot mode tests passed"
