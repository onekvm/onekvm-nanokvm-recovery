#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
ONEKVM_RECOVERY_LIBRARY_ONLY=1 . "$root/initramfs/onekvm-recovery-install-fwup"
progress=$work/progress
root_slot_bytes=1024
blockdev() { echo 4096; }
python3 - "$work" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);(p/'source').write_bytes(b'P'*512+b'R'*1024+b'T'*4096);(p/'dest').write_bytes(b'Z'*4096)
PY
write_root_dd "$work/source" 1 "$work/dest" 1024
python3 - "$work/dest" <<'PY'
from pathlib import Path
import sys
assert Path(sys.argv[1]).read_bytes()==b'R'*1024+b'Z'*3072
PY
if (write_root_dd "$work/source" 1 "$work/dest" 1025); then echo 'slot overflow accepted'; exit 1; fi
blockdev() { echo 512; }
if (write_root_dd "$work/source" 1 "$work/dest" 1024); then echo 'small destination accepted'; exit 1; fi
echo 'storage selection and bounded Recovery writes passed'
