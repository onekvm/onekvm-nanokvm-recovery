#!/bin/sh
# An unsuccessful FAT check cannot select automatic Recovery; explicit entry
# and the normal ten-attempt policy must still work.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'onekvm_storage_links() { :; }\n' >"$work/storage.sh"
sed -e "s|/usr/lib/onekvm/storage-layout.sh|$work/storage.sh|g" \
    -e "s|/run/onekvm-boot-fat/status|$work/status|g" \
    -e "s|/run/onekvm-recovery-boot|$work/boot|g" \
    "$root/initramfs/20-recovery" >"$work/recovery.sh"
. "$work/recovery.sh"
info() { :; }
msg() { :; }
fatal() { echo "$*" >&2; exit 1; }
recovery_wait_for() { :; }
mount() { return 1; }
loader=/dev/unused
[ -z "$(recovery_read_nextboot_mode)" ]
[ ! -e "$work/boot" ]
recovery_read_nextboot_mode() { printf '%s\n' "$requested"; }
recovery_requested_by_cmdline() { [ "$explicit" = cmdline ]; }
recovery_boot_button_held() { [ "$explicit" = button ]; }
recovery_consume_nextboot_mode() { echo consumed >>"$work/trace"; }
recovery_wait_for() { :; }
recovery_record_boot_attempt() { echo counter >>"$work/trace"; return "$counter_rc"; }
recovery_load_payload() { :; }
recovery_assert_sd_unmounted() { :; }
recovery_start_oled() { :; }
recovery_start_gadget() { :; }
recovery_console() { echo recovery >>"$work/trace"; }
loader=/dev/unused
requested=; explicit=; counter_rc=1
printf 'failed\n' >"$work/status"
recovery_run
[ ! -e "$work/trace" ]
for explicit in cmdline button; do
    recovery_run
    [ "$(cat "$work/trace")" = recovery ]
    rm "$work/trace"
done
explicit=; requested=recovery
recovery_run
[ "$(cat "$work/trace")" = "$(printf 'consumed\nrecovery')" ]
rm "$work/trace"
requested=
for status in clean repaired missing; do
    printf '%s\n' "$status" >"$work/status"
    [ "$status" != missing ] || rm "$work/status"
    recovery_run
    [ "$(cat "$work/trace")" = "$(printf 'counter\nrecovery')" ]
    rm "$work/trace"
done
counter_rc=0
recovery_run
[ "$(cat "$work/trace")" = counter ]
echo 'FAT failure bypass, explicit entry and normal retry policy passed'
