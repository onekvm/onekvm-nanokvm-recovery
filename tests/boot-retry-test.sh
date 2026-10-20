#!/bin/sh
# Exercise the real counter using a fake FAT mount, without touching devices.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
. "$root/initramfs/20-recovery"
filesystem=$work/fat
mkdir "$filesystem"
loader=/dev/test-boot
msg() { printf '%s\n' "$*"; }
info() { :; }
fatal() { echo "$*" >&2; exit 1; }
sync() { :; }
mount() {
    [ "${fail_mount:-0}" = 0 ] || return 1
    [ "$1:$2:$3:$4:$5" = "-t:vfat:-o:rw:$loader" ]
    rmdir "$6"
    ln -s "$filesystem" "$6"
}
umount() {
    [ "${fail_umount:-0}" = 0 ] || return 1
    rm "$1"
    mkdir "$1"
}
counter=$filesystem/boot_retry_time.txt

attempt=1
while [ "$attempt" -lt 10 ]; do
    recovery_record_boot_attempt "$work/mount"
    [ "$(wc -l <"$counter")" -eq "$attempt" ] && [ ! -e "$work/mount" ]
    attempt=$((attempt + 1))
done
if recovery_record_boot_attempt "$work/mount"; then
    echo 'tenth boot did not require Recovery' >&2; exit 1
fi
[ "$(wc -l <"$counter")" -eq 10 ] && [ ! -e "$work/mount" ]
if recovery_record_boot_attempt "$work/mount"; then
    echo 'uncleared failed boots bypassed Recovery' >&2; exit 1
fi
[ "$(wc -l <"$counter")" -eq 11 ]

# A successful boot or explicit Recovery retry starts a new ten-boot window.
rm "$counter"
recovery_record_boot_attempt "$work/mount"
[ "$(cat "$counter")" = 1 ]
printf '1\r\n' >"$counter"
recovery_record_boot_attempt "$work/mount"
[ "$(wc -l <"$counter")" -eq 2 ]

# A partially written record must never be mistaken for a fresh first boot.
for content in invalid 1 3; do
    printf '%s' "$content" >"$counter"
    if recovery_record_boot_attempt "$work/mount"; then
        echo 'corrupt boot counter bypassed Recovery' >&2; exit 1
    fi
done
rm "$counter"
fail_mount=1
if recovery_record_boot_attempt "$work/mount"; then
    echo 'unrecorded boot bypassed Recovery' >&2; exit 1
fi
[ ! -e "$counter" ] && [ ! -e "$work/mount" ]
fail_mount=0
mkdir "$counter"
if recovery_record_boot_attempt "$work/mount" 2>/dev/null; then
    echo 'unwritable boot counter bypassed Recovery' >&2; exit 1
fi
rmdir "$counter"
fail_umount=1
if (recovery_record_boot_attempt "$work/mount"); then
    echo 'boot continued with a mounted Recovery partition' >&2; exit 1
fi
echo 'boot retry threshold, persistence, corruption and mount failure tests passed'

# A verified firmware install must release the old retry window only after
# writing the paired boot payload and selecting the repaired slot.
ONEKVM_RECOVERY_LIBRARY_ONLY=1 . "$root/initramfs/onekvm-recovery-install-fwup"
boot=$work/repaired-boot
mkdir "$boot"
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    printf '1\n'
done >"$boot/boot_retry_time.txt"
printf 'new fit\n' >"$work/onekvm-nanokvm-boot-b-onekvm-nanokvm.itb"
printf 'new build\n' >"$work/onekvm-nanokvm-build-id"
printf 'recovery tools\n' >"$work/onekvm-recovery.zip"
storage_media=nanokvm
loader_size=262144
kind=fwup
target=B
lower=b
storage_backend=/bin/true
mount() { :; }
umount() { :; }
install_boot
[ ! -e "$boot/boot_retry_time.txt" ]
cmp "$boot/boot-b.itb" "$work/onekvm-nanokvm-boot-b-onekvm-nanokvm.itb"
echo 'successful Recovery installation resets boot attempts'
