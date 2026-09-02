# NanoKVM initramfs and Recovery flow

English | [简体中文](initramfs-flow.zh-CN.md)

This document describes the implemented NanoKVM boot order, one-shot boot
mode, Recovery mount boundaries, and OpenEmbedded build entry points. Here,
"unmounted" refers to SD block partitions. The initramfs still mounts pseudo
filesystems such as `/proc`, `/sys`, `/dev`, `/run`, configfs, and tmpfs.

## Image composition

`onekvm-initramfs` is embedded in both A/B FIT boot images. NanoKVM adds these
modules to the standard `initramfs-framework` flow:

- `/init.d/15-nanokvm-firstboot` creates or repairs the A/B and userdata layout.
- `/init.d/20-recovery` reads the one-shot mode and either handles factory reset
  or takes over the boot in Recovery.
- `/init.d/89-partlabel` resolves `PARTLABEL=` parameters.
- `/init.d/90-rootfs` mounts the selected read-only SquashFS root slot.
- `/init.d/91-overlayroot` mounts userdata, creates the overlay, and calls
  `switch_root`.

Large Recovery components stay outside the FIT. The web UI, OpenSSL, hash
tools, installer, and OLED modules live in `recovery.zip` on the boot FAT and
are unpacked into the in-memory root only after Recovery has been selected.

## Where the Recovery Web UI comes from

The Recovery Web UI source lives in the adjacent `onekvm-ui` repository, not
in this repository:

- `onekvm-ui/recovery.html` is the standalone HTML entry.
- `onekvm-ui/src/recovery/main.ts` starts Vue.
- `onekvm-ui/src/recovery/App.vue` implements the page.
- `onekvm-ui/src/recovery/api.ts` calls `/status`, `/progress`, `/firmware`,
  `/reset-user`, `/factory-reset`, and `/reboot`.
- `onekvm-ui/src/recovery/messages.ts` contains English, Simplified Chinese,
  and Traditional Chinese copy.

It is a second Vite build in the same source repository as the normal console,
using `onekvm-ui/vite.recovery.config.ts`. The configuration disables
`public/`, uses a single entry and `inlineDynamicImports`, then inlines all
generated JavaScript and CSS into:

```text
onekvm-ui/dist-recovery/index.html
```

The build plugin rejects remaining `/assets/` references and checks that the
final HTML contains the firmware, reset-user, factory-reset, and reboot API
paths. The result is one self-contained HTML file. Recovery needs no Node.js
and does not depend on the normal system's `/usr/share/onekvm-ui`.

Before starting the kas container, `onekvm-distro/scripts/kas.sh` invokes
`scripts/oe-ui-artifact.sh`:

1. It requires Node.js 22.12 or newer from the 22.x line and installs exactly
   from the lockfile with `pnpm install --frozen-lockfile` or `npm ci`.
2. `pnpm run build` (or `npm run build` for an npm-locked tree) emits both
   the normal `dist/` and Recovery `dist-recovery/` outputs.
3. It creates a reproducible Recovery artifact with fixed timestamps, uid/gid,
   file ordering, and `gzip -n`:

   ```text
   onekvm-distro/.oe/artifacts/onekvm-ui-recovery-dist.tar.gz
   ```

4. An input SHA-256 over sources, edition, and Node/package-manager versions
   controls caching. Any input change rebuilds the artifact. Pro edition first
   overlays `onekvm-pro-ui/src/product`, then uses the same Recovery build.

`onekvm-recovery_1.0.bb` takes `index.html` from
`/work/artifacts/onekvm-ui-recovery-dist.tar.gz` and installs it into the
Recovery payload at:

```text
/usr/share/onekvm-recovery/index.html
```

The same payload contains the C web server, TLS certificate and key, OpenSSL
libraries, flash/hash tools, and OLED modules. The recipe packages all of it as
`onekvm-recovery.zip`. The SD installer places the zip on the boot FAT, and the
RAUC `.fwup` also carries it so the bundle hook or Recovery installer can
update `/boot/recovery.zip`.

After `20-recovery` unpacks the zip into the initramfs `/`, the C
`onekvm-recovery-web` program serves static files from
`/usr/share/onekvm-recovery` by default. It listens on HTTP port 80, attempts
HTTPS on port 443, and implements the same-origin APIs listed above in the same
process.

Consequently, a local Vite build after changing the Recovery UI is useful for
checking `dist-recovery/index.html`, but it does not update a device. The
authorized deployment path must rebuild the SD image or `.fwup` containing
`recovery.zip` through `onekvm-distro`/kas. Do not commit `dist-recovery/` or
`.oe/` outputs.

## `/init` and module order

OpenEmbedded's `initramfs-framework` `/init` mounts procfs, sysfs, the `/run`
tmpfs, and devtmpfs; parses the kernel command line into `bootparam_*`
variables; then runs enabled `/init.d/` modules in filename order.

On an already initialized device, the important path is:

```text
/init
  -> 15-nanokvm-firstboot (returns after confirming a complete layout)
  -> 20-recovery
       -> temporarily mount mmcblk0p1 read-only, read nextboot-mode.txt, unmount
       -> factory-reset: consume mode, format userdata, resume normal boot
       -> recovery: consume mode, load recovery.zip, remain in initramfs
       -> no mode: resume normal boot
  -> 89-partlabel
  -> 90-rootfs (mount selected A/B SquashFS)
  -> 91-overlayroot (mount userdata and switch_root)
```

`15-nanokvm-firstboot` runs before the Recovery hook. On the original
two-partition installer layout it may rewrite the partition table, populate
A/B, create userdata, and temporarily mount partitions for verification. The
Recovery mount guarantee therefore applies to an already initialized A/B
layout, not to first-boot migration.

## One-shot boot mode

The normal system requests the next boot through `/boot/nextboot-mode.txt`.
Only the first line is read. Accepted values are:

```text
recovery
factory-reset
```

Writers emit LF. The reader removes CR from the first line, so Windows CRLF
files such as `recovery\r\n` are accepted. Unknown values trigger no action and
are retained. A valid value is removed through a short read-write boot FAT
mount before its action starts. Failure to consume it is fatal, preventing a
mode from accidentally repeating.

Selection priority is `factory-reset`, kernel `onekvm_recovery=1`, held BOOT,
`recovery`, then normal boot. `onekvm-recovery` writes `recovery`, while
`onekvm-factory-reset` writes `factory-reset`. Both write a temporary file,
sync it, rename it in the same directory, and sync again.

## Recovery entry and mount boundary

The boot FAT lifecycle when entering Recovery is:

1. Mount `/dev/mmcblk0p1` read-only, read `nextboot-mode.txt`, and unmount it.
2. For a valid mode, mount it read-write, remove the mode file, and unmount it.
3. Mount it read-only, unpack `recovery.zip` into the memory root, and unmount it.
4. Check `/proc/mounts` and refuse to start Recovery services if any of
   `/dev/mmcblk0p1` through `p4` remains mounted.

Consequently, once the web, network, OLED, serial console, and USB gadget are
running, the local system has no persistent mount of boot, rootfs A, rootfs B,
or userdata. The later rootfs and overlayroot modules never run.

Unmounted does not mean unexposed: the default USB mass-storage gadget exports
the whole raw `/dev/mmcblk0` disk read-write to the USB host. The Recovery
system does not locally mount its partitions, but the host can. Operations
that write storage locally first unbind the gadget.

The idle Recovery environment still mounts these non-SD filesystems:

| Mount | Purpose |
| --- | --- |
| procfs, sysfs, devtmpfs | initramfs runtime |
| `/run` tmpfs | runtime state |
| configfs | USB mass-storage and CDC ACM gadget |
| `/run/onekvm-firmware` tmpfs | firmware upload, capped at 80% of RAM by default |

## Storage effects of Recovery actions

| Action | Partition behavior |
| --- | --- |
| Enter/idle only | Boot FAT is mounted only during entry; no SD partition remains mounted when services start |
| Reset default web user | Unbind gadget and mount userdata read-write; the current implementation keeps it mounted until reboot |
| Factory reset | Unbind gadget, format `mmcblk0p4` without mounting it, then reboot |
| Install `.fwup` | Unbind gadget, write the inactive rootfs slot directly, and temporarily mount boot FAT read-write to update FIT, build ID, `recovery.zip`, and `uEnv.txt` |
| Install full `.img` | Unbind gadget, loop-mount the image boot FAT read-only, write rootfs A directly, and temporarily mount the device boot FAT read-write |
| Reboot | The entry mode was already consumed, so reboot does not mount boot FAT to clear it |

## Build and verification

The target initramfs, FIT, `recovery.zip`, SD image, and bundle must be built by
the `onekvm-distro` OpenEmbedded/kas flow. Do not cross-compile or assemble
target artifacts outside it:

```sh
cd onekvm-distro
make nanokvm-sd
```

Use `make nanokvm-build` for the complete NanoKVM output. The relevant recipes
are:

- `meta-onekvm/recipes-core/images/onekvm-initramfs.bb`
- `meta-onekvm-bsp/recipes-support/onekvm-recovery/onekvm-recovery_1.0.bb`
- `meta-onekvm-bsp/recipes-bsp/onekvm-boot/onekvm-boot-image.bb`

The repository-level `make test` only runs host unit tests and helper
self-tests; it does not produce deployable target artifacts. OpenEmbedded
outputs stay under `onekvm-distro/.oe/` and are not committed.
