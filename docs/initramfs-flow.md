# NanoKVM initramfs and Recovery flow

English | [简体中文](initramfs-flow.zh-CN.md)

This document describes the shared NanoKVM Cube/PCIe and Pro (SD/eMMC) boot order, one-shot boot
mode, Recovery mount boundaries, and OpenEmbedded build entry points. Here,
"unmounted" refers to SD block partitions. The initramfs still mounts pseudo
filesystems such as `/proc`, `/sys`, `/dev`, `/run`, configfs, and tmpfs.

## Image composition

`onekvm-initramfs` is embedded in Cube/PCIe A/B FITs; Pro uses a legacy
ramdisk per slot. Both add these
modules to the standard `initramfs-framework` flow:

- The automatic installer layer adds `/init.d/12-installer` on NanoKVM SD.
  With an installation token it starts the built-in early OLED indicator,
  then runs the RAM installer before partition migration or normal Recovery.
- `/init.d/15-nanokvm-firstboot` creates or repairs the A/B and userdata layout.
- `/init.d/20-recovery` reads the one-shot mode and either handles factory reset
  or takes over the boot in Recovery.
- `/init.d/89-partlabel` resolves `PARTLABEL=` parameters.
- `/init.d/90-rootfs` mounts the selected read-only SquashFS root slot.
- `/init.d/91-overlayroot` mounts userdata, creates the overlay, and calls
  `switch_root`.

Large Recovery components stay outside both A/B initramfs copies. The web UI, OpenSSL, hash
tools, offline RAUC verifier, firmware installer, and runtime OLED/Pro framebuffer modules live in `recovery.zip` on the boot FAT and
are unpacked into the in-memory root only after Recovery has been selected.
The NanoKVM early OLED indicator and the automatic layer's RAM installer
are included in the initramfs because they run before this archive is loaded.

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
       -> temporarily mount the resolved boot partition read-only, read nextboot-mode.txt, unmount
       -> factory-reset: consume mode, format userdata, resume normal boot
       -> recovery: consume mode, load recovery.zip, remain in initramfs
       -> no mode: append a boot attempt; attempt 3 enters Recovery, otherwise resume boot
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

Selection priority is `factory-reset`, kernel `onekvm_recovery=1`, held BOOT (Pro: rotary KEY_ENTER),
`recovery`, boot retry limit, then normal boot. `onekvm-recovery` writes `recovery`, while
`onekvm-factory-reset` writes `factory-reset`. Both write a temporary file,
sync it, rename it in the same directory, and sync again.

## Boot attempts

Before a normal boot, `20-recovery` temporarily mounts the boot FAT read-write,
reads `boot-retry-policy.txt`, then appends one `1\n` record to
`boot_retry_time.txt`, syncs it, and unmounts it. A missing or unknown policy
is enabled; `disabled`, `false`, and `0` remove the stale retry counter and
continue the normal boot.
Attempts 1 through 9 continue to rootfs; attempt 10 enters Recovery immediately.
Malformed records or a counter read/write failure also select Recovery.
Explicit Recovery entry does not append a record. Factory reset starts a new
retry window. Recovery retains the counter for diagnosis; successfully
installing firmware, factory reset, or the Recovery web reboot action clears it.
The reboot helper first releases USB mass storage before mounting boot FAT.

In the normal system, `onekvm-boot-success.timer` waits until `multi-user.target`
and A/B slot validation have completed. After another two minutes, the cleanup
service checks that OneKVM and slot validation remain active, deletes the
counter, and syncs the filesystem. A reboot before that leaves the attempts in
place. This detects failures after initramfs starts; failures in ROM, U-Boot,
or the kernel before initramfs cannot update this file.

## Recovery entry and mount boundary

The boot FAT lifecycle when entering Recovery is:

1. Mount the resolved boot partition (Cube: `/dev/mmcblk0p1`) read-only, read `nextboot-mode.txt`, and unmount it.
2. For a valid mode, mount it read-write, remove the mode file, and unmount it.
   Automatic fallback instead briefly mounts it read-write to record the
   tenth boot attempt, then unmounts it before loading Recovery.
3. Mount it read-only, unpack `recovery.zip` into the memory root, and unmount it.
4. Check `/proc/mounts` and refuse to start Recovery services if any of
   the selected medium’s p1–p4 remains mounted.

Consequently, once the web, network, OLED, serial console, and USB gadget are
running, the local system has no persistent mount of boot, rootfs A, rootfs B,
or userdata. The later rootfs and overlayroot modules never run.

Unmounted does not mean unexposed: the default USB mass-storage gadget exports
the whole raw selected SD disk read-write to the USB host. The Recovery
system does not locally mount its partitions, but the host can. Pro eMMC exposes ACM only, never the writable vendor disk. Operations
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
| Factory reset | Unbind gadget, format the selected userdata partition without mounting it, then reboot |
| Install `.fwup` | Unbind gadget, use offline RAUC to verify CMS/verity/compatibility, mark target bad, then bound writes to the inactive rootfs slot, and temporarily mount boot FAT read-write to update FIT, build ID, `recovery.zip`, and `uEnv.txt` |
| Install full `.img` | Validate the 512-byte metadata/hash trailer against the current medium first; unbind gadget, loop-mount the image boot FAT read-only, bound writes to the inactive rootfs and its paired boot; preserve partition table and bootloader, and temporarily mount the device boot FAT read-write |
| Reboot | Release USB mass storage, briefly mount boot FAT read-write to clear the boot retry counter, unmount, then reboot |

Compact Cube/PCIe `.img` carriers contain only `boot.sd` and declare
`bootclone=A` in a separate marker file. Recovery validates that marker and
matching A/B build IDs before marking any slot bad, then installs the A FIT
under the inactive slot's filename. Legacy images with distinct A/B FITs keep
using their paired payloads; a missing B FIT without the marker is rejected.
The active slot, partition table and bootloader are preserved. Older Recovery
versions require a normal `.fwup` upgrade before they can import this compact
`.img` format; writing the complete image to a new card runs its own firstboot.

## Build and verification

The target initramfs, FIT, `recovery.zip`, SD image, and bundle must be built by
the `onekvm-distro` OpenEmbedded/kas flow. Do not cross-compile or assemble
target artifacts outside it:

```sh
cd onekvm-distro
make image sd
```

Use `make firmware` then `make image sd` for the complete NanoKVM output. The relevant recipes
are:

- `meta-onekvm/recipes-core/images/onekvm-initramfs.bb`
- `meta-onekvm-bsp/recipes-support/onekvm-recovery/onekvm-recovery_1.0.bb`
- `meta-onekvm-bsp/recipes-bsp/onekvm-boot/onekvm-boot-image.bb`

The repository-level `make test` only runs host unit tests and helper
self-tests; it does not produce deployable target artifacts. OpenEmbedded
outputs stay under `onekvm-distro/.oe/` and are not committed.

Pro SD/eMMC layouts, builds and temporary boot limits are described in
`onekvm-distro/docs/nanokvm-pro-ab-recovery.md`. All writes use the shared
storage-layout resolver and refuse unknown or ambiguous root media. New
userdata uses f2fs; existing ext4/f2fs is preserved and A/B share upper/work.

## Recovery networking and one-time entry choices

Clicking Recovery in the normal system opens a modal with a selectable list: reuse the system network
(default every time), independent DHCP, or independent static IPv4. There is no Save action. Editing or
cancelling does not modify user configuration. Independent DHCP enables the simultaneous
`192.168.233.10/24` fallback by default. More options provide interface, MAC and plain DNS settings.
The Recovery page displays the active IP addresses. Network changes after entry are available
through the console and apply only to the current session.

Automatic Recovery uses the confirmed system Ethernet IPv4, effective MAC, gateway and plain DNS from
`/boot/recovery-network.json`. Core checks this snapshot at startup, after system network confirmation,
and after related configuration saves. Pending transactions suspend export. Unchanged contents are not
rewritten; the last valid `.json.bak` is retained. Legacy `recovery.network` experiment settings no longer
select the default. `onekvm-cli request system config apply recovery-network` exports only system networking.

`GET /api/system/recovery-network` previews the default; `POST` only validates/previews a supplied choice.
`POST /api/system/recovery` validates this entry's choice and atomically writes its snapshot as line two of
`nextboot-mode.txt`, after the existing `recovery` first line, then requests reboot. Existing boot requests
cannot be overwritten. Initramfs copies the bounded second line to `/run/onekvm-recovery-network.once`
during the read-only marker mount, then consumes the complete marker. The one-time snapshot takes
precedence over the system snapshot, with a visible fallback on validation failure. It does not replace
the system snapshot or save the choice to userdata, and cannot affect another boot.

Snapshot version 1 contains a `network` object and SHA-256 of its compact UTF-8 JSON in this field order:
`source/interface/mode/address/gateway/fallback_enabled/fallback_address/mac_address/use_custom_dns/dns/dhcp_client_id`.
The default MAC is the effective system MAC; system DHCP identity prefers networkd's recorded Client ID.
No account, Wi-Fi or VPN secrets are exported. The bounded parser validates fields, version, addresses
and checksum, and never executes JSON contents.

All boot reads finish before USB MSC is bound. Networking and session changes access RAM without
mounting userdata or exported SD partitions. Missing/unusable snapshots or interfaces fall back to
DHCP plus `192.168.233.10/24`. Lease callbacks preserve fallback addresses and manual DNS; their shared
lock and generation prevent stale callbacks from changing a later session. Factory reset clears system
snapshots and backups.

Only physical Ethernet and IPv4 are rebuilt; IPv6, VLAN, Wi-Fi and VPN are not inherited. Encrypted
DNS/DNSSEC requires an explicit plain Recovery DNS override. Existing system fallback addresses,
including `.1`, are preserved. Reusing system static IPv4 does not add another fallback address.

These console commands apply only to the current session:

```sh
onekvm-recovery-network status
onekvm-recovery-network dhcp
onekvm-recovery-network static 10.100.99.107/24 10.100.99.1
onekvm-recovery-network fallback 192.168.233.10/24
```

`GET /network` reports applied settings/source; `POST /network` applies session changes. `GET /status`
reports every IPv4 address plus `networkSource`/`ipv4Mode`. Serial banners label DHCP/STATIC/FALLBACK;
Monochrome OLEDs show a fixed `RECOVERY` heading and cycle through addresses below it.
The Pro Desk shows a static color screen matching its initialization layout: the
OneKVM wordmark, all IPv4 addresses together, and a blue `Recovery mode` label.
It redraws only when network status changes and never periodically blanks.
The web page periodically refreshes the displayed IP addresses.
