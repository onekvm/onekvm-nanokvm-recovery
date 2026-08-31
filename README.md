# onekvm-nanokvm-recovery

English | [简体中文](README.zh-CN.md)

OneKVM USB recovery and factory-reset support for NanoKVM. It includes the
initramfs recovery hook, runtime marker commands, recovery console, input
helper, systemd integration, and a recovery web UI on TCP 80/443.

The recovery web server on TCP 80/443 serves the Recovery UI built by
`onekvm-ui` (`dist-recovery/`). That page only exposes firmware flash
(`.fwup` to the inactive A/B slot), reset of the default Web user, and reboot.
Serial/USB consoles and USB mass storage remain available.

## Build

```sh
make
```

The target packages are built by the recipe in the `meta-onekvm` layer.

## License

MIT. See [LICENSE](LICENSE).
