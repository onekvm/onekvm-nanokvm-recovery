#!/bin/sh
# Exercise actual Recovery payload selection and atomic file installation.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
ONEKVM_RECOVERY_LIBRARY_ONLY=1 . "$root/initramfs/onekvm-recovery-install-fwup"
storage_media=nanokvm
loader_size=262144
loader=/dev/unused
kind=image
mount() { :; }
umount() { :; }
sync() { :; }
storage_backend=$work/bootchooser
cat >"$storage_backend" <<'SH'
#!/bin/sh
printf '%s\n' "$*" >>"$chooser_log"
SH
chmod +x "$storage_backend"

prepare() {
    case_dir=$work/$1
    img_fat=$case_dir/image
    boot=$case_dir/installed
    chooser_log=$case_dir/chooser.log
    export chooser_log
    mkdir -p "$img_fat" "$boot"
    printf 'new A FIT\n' >"$img_fat/boot.sd"
    printf 'build\n' >"$img_fat/build-id-a"
    cp "$img_fat/build-id-a" "$img_fat/build-id-b"
    printf 'new Recovery\n' >"$img_fat/recovery.zip"
    printf 'A\n' >"$img_fat/bootclone"
    printf 'old A FIT\n' >"$boot/boot.sd"
    printf 'old B FIT\n' >"$boot/boot-b.itb"
    printf 'old A build\n' >"$boot/build-id-a"
    printf 'old B build\n' >"$boot/build-id-b"
    printf '1\n' >"$boot/boot_retry_time.txt"
}

for target in A B; do
    case "$target" in A) lower=a; other=b; other_fit=boot-b.itb ;; B) lower=b; other=a; other_fit=boot.sd ;; esac
    prepare compact-$target
    cp "$boot/$other_fit" "$case_dir/untouched-fit"
    cp "$boot/build-id-$other" "$case_dir/untouched-id"
    select_nanokvm_image_boot
    [ "$fit" = "$img_fat/boot.sd" ]
    install_boot
    cmp "$boot/$other_fit" "$case_dir/untouched-fit"
    cmp "$boot/build-id-$other" "$case_dir/untouched-id"
    case "$target" in A) destination=boot.sd ;; B) destination=boot-b.itb ;; esac
    cmp "$boot/$destination" "$img_fat/boot.sd"
    cmp "$boot/build-id-$lower" "$img_fat/build-id-$lower"
    cmp "$boot/recovery.zip" "$img_fat/recovery.zip"
    [ ! -e "$boot/boot_retry_time.txt" ]
    [ "$(cat "$chooser_log")" = "$(printf 'set-state %s good\nset-primary %s' "$target" "$target")" ]
done

prepare legacy
target=B; lower=b
rm "$img_fat/bootclone"
printf 'legacy B FIT\n' >"$img_fat/boot-b.itb"
install_boot
cmp "$boot/boot-b.itb" "$img_fat/boot-b.itb"

for damage in no-marker unknown-marker mismatched-id empty-b missing-a; do
    prepare "$damage"
    case "$damage" in
        no-marker) rm "$img_fat/bootclone" ;;
        unknown-marker) printf 'B\n' >"$img_fat/bootclone" ;;
        mismatched-id) printf 'other build\n' >"$img_fat/build-id-b" ;;
        empty-b) : >"$img_fat/boot-b.itb" ;;
        missing-a) rm "$img_fat/boot.sd" ;;
    esac
    if select_nanokvm_image_boot; then
        echo "accepted damaged image: $damage" >&2; exit 1
    fi
    [ ! -e "$chooser_log" ]
    [ "$(cat "$boot/boot-b.itb")" = 'old B FIT' ]
done
echo 'compact/legacy Recovery image installation and inactive-slot preservation passed'
