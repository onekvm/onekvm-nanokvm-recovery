#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

. "$root/initramfs/20-recovery"

# Use a host block node only for the file-type check. The blockdev helper is
# overridden below, so the test never reads or writes the host disk.
disk=/dev/sda

calls_file=$work/calls
printf '0\n' >"$calls_file"
blockdev() {
    calls=$(cat "$calls_file")
    calls=$((calls + 1))
    printf '%s\n' "$calls" >"$calls_file"
    if [ "$calls" -lt 3 ]; then
        echo 0
    else
        echo 4294967296
    fi
}
recovery_wait_for_block_device "$disk"
[ "$(cat "$calls_file")" -eq 3 ]

blockdev() { echo 0; }
if recovery_wait_for_block_device "$disk"; then
    echo 'zero-sized block device accepted' >&2
    exit 1
fi

echo 'block-device capacity wait passed'
