#!/bin/sh
# The Recovery gadget exports the card and a console, names itself after the
# board, and lets the board hold the controller until its USB PHY is ready.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/udc/ffb00000.usb" "$work/role"
printf 'configfs %s/config configfs rw 0 0\n' "$work" >"$work/mounts"
sed -e "s|/sys/kernel/config|$work/config|g" \
    -e "s|/proc/mounts|$work/mounts|g" \
    -e "s|/proc/cviusb/otg_role|$work/no-cviusb|g" \
    -e "s|/sys/class/usb_role|$work/role|g" \
    -e "s|/sys/class/udc|$work/udc|g" \
    "$root/initramfs/20-recovery" >"$work/recovery.sh"
. "$work/recovery.sh"
fatal() { echo "$*" >&2; exit 1; }
recovery_wait_for_block_device() { :; }
# configfs creates each function's first LUN and the parents of a new item.
mkdir() {
    command mkdir -p "$@"
    for dir; do
        case "$dir" in functions/mass_storage.*) command mkdir -p "$dir/lun.0" ;; esac
    done
}
gadget=$work/config/usb_gadget/onekvm-recovery
storage_media=picokvm-sd
disk=/dev/mmcblk1

# By default nothing waits, and the product keeps its existing name.
(recovery_start_gadget)
[ "$(cat "$gadget/UDC")" = ffb00000.usb ]
[ "$(cat "$gadget/strings/0x409/product")" = 'OneKVM NanoKVM Recovery' ]
[ "$(cat "$gadget/functions/mass_storage.sd/lun.0/file")" = /dev/mmcblk1 ]
[ -L "$gadget/configs/c.1/mass_storage.sd" ] && [ -L "$gadget/configs/c.1/acm.GS0" ]
[ "$(cat "$gadget/idVendor"):$(cat "$gadget/idProduct")" = 0x3346:0x1009 ]
rm -rf "$work/config/usb_gadget"

# A board's hook receives the controller and runs before the gadget binds.
(
    ONEKVM_RECOVERY_USB_PRODUCT='OneKVM PicoKVM Recovery'
    recovery_board_usb_ready() {
        [ ! -e "$gadget/UDC" ] || fatal 'bound before the board was ready'
        printf '%s\n' "$1" >"$work/ready"
    }
    recovery_start_gadget
)
[ "$(cat "$work/ready")" = ffb00000.usb ]
[ "$(cat "$gadget/UDC")" = ffb00000.usb ]
[ "$(cat "$gadget/strings/0x409/product")" = 'OneKVM PicoKVM Recovery' ]
echo 'recovery gadget passed'
