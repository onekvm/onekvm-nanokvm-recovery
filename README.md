# onekvm-nanokvm-recovery

English | [简体中文](README.zh-CN.md)

OneKVM USB recovery and factory-reset support for NanoKVM. It includes the
initramfs recovery hook, runtime marker commands, recovery console, input
helper, systemd integration, and a recovery web UI on TCP 80/443.

The initramfs only contains the USB gadget, consoles, and a hook that unpacks
`recovery.zip` from the FAT `/boot` partition. OpenSSL, the recovery web,
SHA-256, firmware installer, and the install/recovery OLED `.ko` modules stay
out of the 16 MiB FIT. The zip is updated with the `.fwup` bundle.

The recovery web server on TCP 80/443 serves the Recovery UI built by
`onekvm-ui` (`dist-recovery/`). That page only exposes firmware flash
(`.fwup` to the inactive A/B slot), reset of the default Web user, and reboot.
After DHCP, the OLED shows `RECOVERY` and the IPv4 address via C906L
(`/dev/onekvm-oled`). The same URLs are printed on the serial/USB console.
Firmware uploads go to a dedicated tmpfs at `/run/onekvm-firmware` sized to
80% of RAM, so a full `.fwup` does not depend on userdata (which may be
damaged). The file cannot exceed the free space on that tmpfs. Recovery hashes the
upload with SHA-256, checks image hashes in `manifest.raucm`, and re-hashes
the written rootfs slot.
Serial/USB consoles and USB mass storage remain available.

## Build

```sh
make
```

The target packages are built by the recipe in the `meta-onekvm` layer.

## License

MIT. See [LICENSE](LICENSE).
