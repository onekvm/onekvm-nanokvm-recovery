# onekvm-nanokvm-recovery

[English](README.md) | 简体中文

OneKVM 面向 NanoKVM 的 USB 恢复和恢复出厂设置支持，包括 initramfs recovery
hook、运行时标记命令、恢复控制台、输入辅助程序、systemd 集成，以及监听
80/443 的恢复网页。

initramfs 只保留 USB gadget、控制台，以及从 FAT `/boot` 解压 `recovery.zip`
的钩子。OpenSSL、恢复网页、SHA-256 和刷写程序不进 16 MiB 的 FIT。zip 随
`.fwup` 一起更新。

80/443 上的恢复网页使用 `onekvm-ui` 打包的 Recovery UI（`dist-recovery/`）。
页面只提供固件刷写（把 `.fwup` 写到未启动的 A/B 槽）、重置默认 Web 用户和
重启。拿到 DHCP 地址后，OLED 通过 C906L（`/dev/onekvm-oled`）显示
`RECOVERY` 和 IPv4；串口/USB 控制台会打印同样的 URL。固件上传放到单独的 tmpfs（`/run/onekvm-firmware`，大小为内存的 80%），
不依赖可能已经损坏的 userdata。文件不能超过这块 tmpfs 的剩余空间。上传时计算 SHA-256，对照
`manifest.raucm` 校验镜像，写入槽后再核对 rootfs。串口/USB 控制台和
USB 大容量存储仍然可用。

## 编译

```sh
make
```

目标软件包由 `meta-onekvm` layer 中的配方构建。

## 许可证

MIT，详见 [LICENSE](LICENSE)。
