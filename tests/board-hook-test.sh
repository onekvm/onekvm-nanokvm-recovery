#!/bin/sh
# Boards with their own init: the board hook, the counter and web switches,
# the board-drawn screen and the physical console from the kernel command line.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'onekvm_storage_links() { :; }\n' >"$work/storage.sh"
sed -e "s|/usr/lib/onekvm/storage-layout.sh|$work/storage.sh|g" \
    -e "s|/run/onekvm-boot-fat/status|$work/status|g" \
    "$root/initramfs/20-recovery" >"$work/recovery.sh"
. "$work/recovery.sh"
info() { :; }
msg() { :; }
fatal() { echo "$*" >&2; exit 1; }

export ONEKVM_CMDLINE_FILE=$work/cmdline
printf 'earlycon=uart8250,mmio32,0x06080000 rauc.slot=A\n' >"$ONEKVM_CMDLINE_FILE"
[ "$(recovery_console_tty)" = ttyS0 ]
printf 'console=tty0 console=ttyS3,115200n8 rauc.slot=A\n' >"$ONEKVM_CMDLINE_FILE"
[ "$(recovery_console_tty)" = ttyS3 ]
printf 'console=ttyAMA0 console=ttyS1\n' >"$ONEKVM_CMDLINE_FILE"
[ "$(recovery_console_tty)" = ttyS1 ]
printf 'console=ttyS2/../x console=tty0\n' >"$ONEKVM_CMDLINE_FILE"
[ "$(recovery_console_tty)" = ttyS0 ]

# A board that draws its own screen gets no OLED attempt or warning.
(
    storage_media=go-sd
    info() { echo "$*" >>"$work/trace"; }
    msg() { echo "$*" >>"$work/trace"; }
    recovery_load_oled() { echo oled >>"$work/trace"; }
    recovery_start_oled
)
[ ! -e "$work/trace" ]

# The marker reader leaves the boot FAT mounted; every way out releases it.
recovery_read_nextboot_mode() { nextboot_mode=$requested; recovery_boot_is_mounted=true; }
umount() { echo umount >>"$work/trace"; }
recovery_requested_by_cmdline() { [ "$explicit" = cmdline ]; }
recovery_boot_button_held() { [ "$explicit" = button ]; }
recovery_consume_nextboot_mode() { echo consumed >>"$work/trace"; }
recovery_wait_for() { :; }
recovery_record_boot_attempt() { echo counter >>"$work/trace"; return 1; }
recovery_load_payload() { echo payload >>"$work/trace"; }
recovery_assert_sd_unmounted() { :; }
recovery_start_oled() { :; }
recovery_start_gadget() { :; }
recovery_console() { echo recovery >>"$work/trace"; }
loader=/dev/unused

# The default hook does nothing; a board's replacement runs before the payload.
requested=; explicit=button
recovery_run
[ "$(cat "$work/trace")" = "$(printf 'payload\numount\nrecovery')" ]
rm "$work/trace"
recovery_board_enter() { echo board >>"$work/trace"; }
recovery_run
[ "$(cat "$work/trace")" = "$(printf 'board\npayload\numount\nrecovery')" ]
rm "$work/trace"

# With the counter switched off, an ordinary boot neither counts nor enters
# Recovery, while explicit requests still do.
explicit=
ONEKVM_RECOVERY_COUNT_BOOTS=0
recovery_run
[ "$(cat "$work/trace")" = umount ]
rm "$work/trace"
requested=recovery
recovery_run
[ "$(cat "$work/trace")" = "$(printf 'consumed\nboard\npayload\numount\nrecovery')" ]
rm "$work/trace"
requested=
ONEKVM_RECOVERY_COUNT_BOOTS=1
recovery_run
[ "$(cat "$work/trace")" = "$(printf 'counter\nboard\npayload\numount\nrecovery')" ]
rm "$work/trace"

# Without the Recovery web, no payload is loaded and no warning is printed.
info() { echo "info: $*" >>"$work/trace"; }
ONEKVM_RECOVERY_WEB=0
recovery_run
[ "$(cat "$work/trace")" = "$(printf 'counter\nboard\numount\nrecovery')" ]
echo 'board hook, counter and web switches and console selection passed'
