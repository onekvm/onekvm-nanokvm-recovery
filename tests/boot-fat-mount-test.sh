#!/bin/sh
# An ordinary boot reads and records boot state through one boot FAT mount and
# releases it before the root switch; Recovery entry releases it before the
# marker tool mounts the card and before the card is exported.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'onekvm_storage_links() { :; }\n' >"$work/storage.sh"
sed -e "s|/usr/lib/onekvm/storage-layout.sh|$work/storage.sh|g" \
    -e "s|/run/onekvm-boot-fat/status|$work/fat-status|g" \
    -e "s|/run/onekvm-recovery-network.once|$work/network.once|g" \
    -e "s|/usr/sbin/onekvm-recovery-clear-marker|$work/clear-marker|g" \
    -e "s|/usr/sbin/onekvm-recovery-erase-userdata|$work/erase-userdata|g" \
    "$root/initramfs/20-recovery" >"$work/recovery.sh"
. "$work/recovery.sh"
recovery_boot=$work/boot
loader=/dev/unused
info() { :; }
msg() { :; }
fatal() { echo "$*" >&2; exit 1; }
sync() { :; }
recovery_wait_for() { :; }
recovery_requested_by_cmdline() { return 1; }
recovery_boot_button_held() { return 1; }
recovery_start_oled() { :; }
recovery_start_gadget() { :; }
recovery_console() { echo console >>"$work/trace"; }
# The card stays where the test put it; the state file stands for the kernel.
mount() {
    echo "mount $*" >>"$work/trace"
    case "$*" in
        "-t vfat -o ro $loader "*)
            [ ! -e "$work/mounted" ] || { echo "boot FAT mounted twice" >&2; exit 1; }
            eval "printf '%s' \"\${$#}\"" >"$work/mounted" ;;
    esac
}
umount() {
    echo "umount $1" >>"$work/trace"
    [ "$(cat "$work/mounted" 2>/dev/null)" = "$1" ] || { echo "umount of $1 not mounted" >&2; exit 1; }
    rm -f "$work/mounted"
}
recovery_assert_sd_unmounted() {
    [ ! -e "$work/mounted" ] || { echo "card still mounted before Recovery" >&2; exit 1; }
    echo unmounted >>"$work/trace"
}
for tool in clear-marker erase-userdata; do
    cat > "$work/$tool" <<EOT
#!/bin/sh
[ ! -e "$work/mounted" ] || { echo "$tool ran with the card mounted" >&2; exit 1; }
echo "$tool \$*" >>"$work/trace"
EOT
    chmod +x "$work/$tool"
done
count() { grep -c "^$1" "$work/trace" || :; }
reset() {
    rm -rf "$work/trace" "$work/mounted" "$recovery_boot"
    mkdir -p "$recovery_boot"
    recovery_boot_is_mounted=false
}
released() { [ ! -e "$work/mounted" ] && [ "$recovery_boot_is_mounted" = false ]; }

# Ordinary boot: one read-only mount, one remount for the counter, one release.
reset
recovery_run
[ "$(count 'mount -t vfat -o ro')" = 1 ]
[ "$(count 'mount -o remount,rw')" = 1 ]
[ "$(count umount)" = 1 ]
released
[ "$(wc -l <"$recovery_boot/boot_retry_time.txt")" = 1 ]

# Disabled counter on a clean disk: still one mount and no write.
reset
printf 'disabled\n' >"$recovery_boot/boot-retry-policy.txt"
recovery_run
[ "$(count 'mount -t vfat -o ro')" = 1 ]
[ "$(count 'mount -o remount')" = 0 ]
[ "$(count umount)" = 1 ]
released

# A requested Recovery releases the card before the marker tool and export.
reset
printf 'recovery\n' >"$recovery_boot/nextboot-mode.txt"
recovery_run
[ "$(sed -n '1,3p' "$work/trace" | cut -d' ' -f1)" = "$(printf 'mount\numount\nclear-marker')" ]
[ "$(tail -n 2 "$work/trace")" = "$(printf 'unmounted\nconsole')" ]
released

# A factory reset releases the card for the tools, then counts the attempt
# through one more mount and releases it before the root switch.
reset
printf 'factory-reset\n' >"$recovery_boot/nextboot-mode.txt"
recovery_run
[ "$(cut -d' ' -f1 "$work/trace" | tr '\n' ' ')" = \
  "mount umount clear-marker erase-userdata clear-marker mount mount umount " ]
grep -qx 'clear-marker --boot-retry' "$work/trace"
released

# The retry limit enters Recovery through the same mount.
reset
for _ in 1 2 3 4 5 6 7 8 9 10; do echo 1; done >"$recovery_boot/boot_retry_time.txt"
recovery_run
[ "$(count 'mount -t vfat -o ro')" = 1 ]
[ "$(tail -n 2 "$work/trace")" = "$(printf 'unmounted\nconsole')" ]
released
echo 'one boot FAT mount per boot; released before the marker tool and Recovery'
