#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
install=$root/initramfs/onekvm-recovery-install-fwup
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

printf 'hsqs' >"$work/update.fwup"
kind=$("$install" --detect "$work/update.fwup")
[ "$kind" = fwup ] || {
    echo "expected fwup, got $kind" >&2
    exit 1
}

python3 - "$work/installer.img" <<'PY'
import sys
path = sys.argv[1]
data = bytearray(512)
data[440:444] = b"\x4d\x56\x4b\x4f"
data[510:512] = b"\x55\xaa"
open(path, "wb").write(data)
PY
kind=$("$install" --detect "$work/installer.img")
[ "$kind" = image ] || {
    echo "expected image, got $kind" >&2
    exit 1
}

python3 - "$work/metadata.img" <<'PY'
import hashlib
import struct
import sys

path = sys.argv[1]
payload = b'payload for trailer verification' * 32
metadata = bytearray(480)
metadata[:16] = b'ONEKVM-IMG-META\0'
struct.pack_into('<HHBBHQHH', metadata, 16, 1, 480, 3, 2, 0,
                 len(payload), 96, 0)
metadata[36:68] = hashlib.sha256(payload).digest()
metadata[68:68 + len('test-build')] = b'test-build'
machine = b'onekvm-nanokvm-pro'
struct.pack_into('<HH', metadata, 164, len(machine), 0)
metadata[168:168 + len(machine)] = machine
body = payload + metadata
open(path, 'wb').write(body + hashlib.sha256(body).digest())
PY
ONEKVM_RECOVERY_LIBRARY_ONLY=1 \
ONEKVM_RECOVERY_SHA256="$root/onekvm-recovery-sha256" \
    . "$install"
onekvm_validate_image_metadata "$work/metadata.img" pro-emmc
if onekvm_validate_image_metadata "$work/metadata.img" pro-sd 2>/dev/null; then
    echo "metadata accepted for wrong storage" >&2
    exit 1
fi
printf X | dd of="$work/metadata.img" bs=1 seek=0 conv=notrunc status=none
if onekvm_validate_image_metadata "$work/metadata.img" pro-emmc 2>/dev/null; then
    echo "tampered metadata image accepted" >&2
    exit 1
fi

printf 'nope' >"$work/junk"
if "$install" --detect "$work/junk" >/dev/null 2>&1; then
    echo "junk file was accepted" >&2
    exit 1
fi

# Root images inside offset images: SquashFS or EROFS, each with its used size.
python3 - "$work" <<'PY'
import struct, sys
work = sys.argv[1]
def write(name, offset, fs, size, bits=12, incompat=0):
    header = bytearray(2048)
    if fs == 'squashfs':
        header[:4] = b'hsqs'
        struct.pack_into('<Q', header, 40, size)
    else:
        struct.pack_into('<I', header, 1024, 0xE0F5E1E2)
        header[1024 + 12] = bits
        struct.pack_into('<I', header, 1024 + 36, size >> bits)
        struct.pack_into('<I', header, 1024 + 80, incompat)
    with open('%s/%s' % (work, name), 'wb') as f:
        f.seek(offset)
        f.write(header)
write('squash.root', 0, 'squashfs', 110305280)
write('erofs.root', 4096, 'erofs', 87261184)
write('wide.root', 0, 'erofs', 87261184, incompat=0x80)
write('emmc-erofs.img', 291241984, 'erofs', 87261184)
PY
[ "$(onekvm_root_image_bytes "$work/squash.root" 0)" = 110305280 ]
[ "$(onekvm_root_image_bytes "$work/erofs.root" 4096)" = 87261184 ]
if onekvm_root_image_bytes "$work/wide.root" 0 >/dev/null; then
    echo "48-bit EROFS root accepted" >&2
    exit 1
fi
if onekvm_root_image_bytes "$work/erofs.root" 0 >/dev/null; then
    echo "root found at the wrong offset" >&2
    exit 1
fi
kind=$("$install" --detect "$work/emmc-erofs.img")
[ "$kind" = emmc-image ] || {
    echo "expected EROFS eMMC image, got $kind" >&2
    exit 1
}

echo "install detect tests passed"
