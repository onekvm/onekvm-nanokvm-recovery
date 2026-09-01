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

printf 'nope' >"$work/junk"
if "$install" --detect "$work/junk" >/dev/null 2>&1; then
    echo "junk file was accepted" >&2
    exit 1
fi

echo "install detect tests passed"
