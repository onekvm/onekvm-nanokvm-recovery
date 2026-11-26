#!/bin/sh
# A board that draws its own screen receives the console banner whenever the
# Recovery addresses change, and only then.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/oled" <<'OLED'
#!/bin/sh
[ "$1" = print ] || exit 0
printf 'OneKVM recovery web (system, dhcp)\n  DHCP http://10.0.0.5/\n  https://10.0.0.5/\n'
OLED
chmod +x "$work/oled"
sed -e "s|/usr/sbin/onekvm-recovery-oled|$work/oled|g" \
    -e "s|/dev/kmsg|$work/kmsg|g" \
    -e "s|/etc/issue|$work/issue|g" \
    -e "s|/sys/class/graphics/fb0/name|$work/no-panel|g" \
    "$root/initramfs/20-recovery" >"$work/recovery.sh"
. "$work/recovery.sh"
console_tty=../${work#/}/tty

# Two rounds with the same addresses: one call. The default hook does nothing.
rounds=0
sleep() { rounds=$((rounds + 1)); [ "$rounds" -lt 4 ] || exit 0; }
(recovery_publish_network)
[ ! -e "$work/addresses" ]
(
    recovery_board_addresses() { printf '%s\n--\n' "$1" >>"$work/addresses"; }
    recovery_publish_network
)
[ "$(grep -c '^--$' "$work/addresses")" = 1 ]
grep -qx '  https://10.0.0.5/' "$work/addresses"
grep -q 'https://10.0.0.5/' "$work/issue"
echo 'board address hook passed'
