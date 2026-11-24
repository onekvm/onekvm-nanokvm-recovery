#!/bin/sh
# Factory reset formats userdata with the file system its board boots from.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin" "$work/sys/mmcblk9p4"
printf '4\n' >"$work/sys/mmcblk9p4/partition"
printf '1000000\n' >"$work/sys/mmcblk9p4/size"
: >"$work/userdata"
for tool in mkfs.ext4 mkfs.f2fs mount umount; do
    printf '#!/bin/sh\necho "%s $*" >>"%s/calls"\n' "$tool" "$work" >"$work/bin/$tool"
    chmod +x "$work/bin/$tool"
done
sed -e "s|/usr/lib/onekvm/storage-layout.sh|$work/layout.sh|" \
    -e "s|/sys/class/block|$work/sys|g" \
    -e "s|/run/onekvm-recovery-network-reset|$work/boot|g" \
    -e 's|\[ -b "\$userdata" \]|[ -e "$userdata" ]|' \
    "$root/initramfs/onekvm-recovery-erase-userdata" >"$work/erase"

erase() {
    cat >"$work/layout.sh" <<LAYOUT
onekvm_storage_select() {
    disk_name=mmcblk9; loader=$work/loader; userdata=$work/userdata
    userdata_uuid=4f4b504b-0000-4000-8000-000000000004
    $1
}
onekvm_storage_validate() { :; }
LAYOUT
    rm -f "$work/calls"
    PATH=$work/bin:$PATH sh "$work/erase" >/dev/null
    grep '^mkfs' "$work/calls"
}

case "$(erase 'userdata_fs=ext4')" in
    "mkfs.ext4 -q -F -L userdata -U 4f4b504b-0000-4000-8000-000000000004 -m 0 "*" $work/userdata") ;;
    *) echo 'PicoKVM userdata is not ext4' >&2; exit 1 ;;
esac
case "$(erase 'userdata_fs=f2fs')" in
    "mkfs.f2fs -f -l userdata -U 4f4b504b-0000-4000-8000-000000000004 $work/userdata") ;;
    *) echo 'f2fs userdata changed' >&2; exit 1 ;;
esac
# An older storage layout without userdata_fs keeps f2fs.
erase ':' | grep -q '^mkfs.f2fs '
echo 'factory reset file system passed'
