# onekvm-nanokvm-recovery

English | [简体中文](README.zh-CN.md)

OneKVM USB recovery and factory-reset support for NanoKVM Cube/PCIe and Pro (SD/eMMC). It includes the
initramfs recovery hook, runtime marker commands, recovery console, input
helper, systemd integration, and a recovery web UI on TCP 80/443.

The initramfs only contains the USB gadget, consoles, and the checks for a
held BOOT button, `/boot/nextboot-mode.txt`, or `onekvm_recovery=1`. The first
line of the mode file may be `recovery` or `factory-reset`; both LF and Windows
CRLF are accepted. A valid mode is consumed before it runs so it cannot repeat
after a reboot. `recovery.zip` is unpacked only after Recovery is entered by
temporarily mounting the FAT `/boot` partition read-only. Recovery verifies
that no SD partition remains mounted before its services start.
OpenSSL, the recovery web, SHA-256, firmware installer, and the runtime
OLED module stay out of the 16 MiB FIT. NanoKVM's small early OLED indicator
is included in the initramfs so first boot and automatic installation can
display their status before `recovery.zip` is available. The zip is
updated with the `.fwup` bundle. Hold BOOT at power-on to enter Recovery.

Before each normal boot, initramfs appends `1` and a newline to
`/boot/boot_retry_time.txt` and syncs the boot partition. The tenth attempt
enters Recovery before mounting rootfs. The normal system deletes the file
two minutes after startup and A/B health validation complete. Explicit
Recovery boots do not add attempts. Recovery retains the counter until a
successful firmware installation, factory reset, or a reboot requested through
the Recovery web page resets it, allowing another normal boot attempt.

The recovery web server on TCP 80/443 serves the Recovery UI built by
`onekvm-ui` (`dist-recovery/`). That page exposes firmware flash (a `.fwup`
update to the inactive A/B slot, or a complete installer `.img` restoring the
inactive slot and its paired boot payload), reset of the default Web user, factory reset, and
reboot. It does not rewrite U-Boot/FIP.
Recovery reuses the confirmed system Ethernet IPv4, MAC and plain DNS settings by
default. Clicking Recovery in the normal system opens a list for this entry only: system, DHCP, or static IPv4;
independent DHCP includes the simultaneous `192.168.233.10/24` fallback by default.
These choices are not saved as defaults. The Recovery page displays the active IP addresses;
session network changes are available through the console.
The OLED cycles through available IPv4 addresses via C906L (`/dev/onekvm-oled`),
and the serial/USB console prints their URLs.
Firmware uploads go to a dedicated tmpfs at `/run/onekvm-firmware` sized to
80% of RAM, so a full `.fwup` does not depend on userdata (which may be
damaged). The file cannot exceed the free space on that tmpfs. Recovery uses offline RAUC to verify the CMS signature, verity payload, and
machine compatibility of `.fwup` before marking the target bad and writing.
It bounds copies to the root slot, re-hashes the written data, installs paired
boot payloads, and activates last. Raw `.img` files require a trusted source
and a published hash; they never rewrite the partition table or bootloader.
Pro displays Recovery and its IP on the JD9853 screen; hold the rotary
button (KEY_ENTER) at boot to enter. Storage follows the actual root device,
including when both SD and eMMC are present. SD exports whole-card USB mass
storage plus ACM; Pro eMMC exposes ACM only. Pro SD still needs its temporary
U-Boot entry; Recovery does not change or save the bootloader environment.

OneKVM `.img` files carry a fixed 512-byte metadata/hash trailer. Recovery checks the
profile, SD/eMMC type, payload length, payload SHA-256, and full-image SHA-256 before
any write. A missing trailer or a media mismatch is rejected. The trailer is not an
eMMC rootfs or boot payload and must not be written into the vendor boot area.

See the [initramfs flow guide](docs/initramfs-flow.md) for module ordering,
boot branches, partition-mount boundaries, and the supported build entry
points.

The offline RAUC verifier, gzip squashfs extractor, full `od`/`dd` commands and
their libraries share an XZ tools payload inside `recovery.zip`, unpacked into
RAM on entry. Signed bundles use gzip for the outer squashfs; root filesystem
compression is independent. New Cube images use a 128 MiB FAT; the Recovery
writer still accepts legacy 32 MiB Cube loaders during upgrades.

## Build

```sh
make
```

The target packages are built by the recipe in the `meta-onekvm` layer.

## License

MIT. See [LICENSE](LICENSE).
