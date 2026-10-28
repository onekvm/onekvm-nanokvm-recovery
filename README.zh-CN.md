# onekvm-nanokvm-recovery

[English](README.md) | 简体中文

OneKVM 面向 NanoKVM Cube/PCIe 和 Pro（SD/eMMC）的 USB 恢复和恢复出厂设置支持，包括 initramfs recovery
hook、运行时标记命令、恢复控制台、输入辅助程序、systemd 集成，以及监听
80/443 的恢复网页。

initramfs 只保留 USB gadget、控制台，以及按住 BOOT、
`/boot/nextboot-mode.txt` 或 `onekvm_recovery=1` 的检测。启动模式文件第一行可为
`recovery` 或 `factory-reset`，LF 与 Windows CRLF 均可；有效模式会在执行前消费，
避免重启后重复执行。`recovery.zip` 只在真正进入 Recovery 之后才从 FAT `/boot`
短暂只读挂载并解压，Recovery 服务启动前会再次确认 SD 分区均未挂载。
OpenSSL、恢复网页、SHA-256、刷写程序和运行时 OLED 模块不进 16 MiB 的 FIT。
NanoKVM 的小型早期 OLED 状态模块随 initramfs 内置，让首启和自动安装在
`recovery.zip` 可用之前显示状态。zip 随 `.fwup` 一起更新。开机按住 BOOT 键可进入
Recovery。

initramfs 在每次正常启动前向 `/boot/boot_retry_time.txt` 追加一行 `1` 并同步到磁盘；
第十次启动在挂载根分区前直接进入 Recovery。正常系统启动完成且 A/B 健康检查通过
两分钟后删除该文件。主动进入 Recovery 不累加次数；Recovery 中保留失败计数，
成功安装固件、恢复出厂设置或通过恢复网页重启时清除，以便重新尝试正常启动。

Core 会写入 `/boot/boot-retry-policy.txt`。文件缺失或内容无法识别时仍默认开启保护；
内容为 `disabled`、`false` 或 `0` 时清理旧计数器并继续正常启动。

firstboot 会在首次挂载前离线检查 boot FAT，异常时尝试一次修复并复检。失败时
本次跳过启动计数和自动 Recovery，继续正常启动；命令行、按键及可读的主动模式
文件入口保留。FAT 无法只读挂载时忽略模式文件。重试策略关闭且没有旧计数时只读
挂载，不重新挂载读写，也不执行同步。

80/443 上的恢复网页使用 `onekvm-ui` 打包的 Recovery UI（`dist-recovery/`）。
页面提供固件刷写（`.fwup` 更新包写入未启动的 A/B 槽，或完整 `.img` 恢复
非活动槽及其配套启动载荷）、重置默认 Web 用户、恢复出厂设置和重启。不会改写
U-Boot/FIP。Recovery 默认复用系统已确认的有线 IPv4、MAC 和普通 DNS；也可在
正常系统点击 Recovery 后，从列表选择本次复用系统网络、独立 DHCP 或静态 IPv4。
这些选择不保存为默认配置。独立 DHCP 默认同时启用
`192.168.233.10/24` 备用地址。恢复网页显示当前 IP 地址；本次会话的网络可通过控制台修改。
OLED 通过 C906L（`/dev/onekvm-oled`）轮流显示各个 IPv4，串口/USB 控制台打印
对应 URL。固件上传放到单独的 tmpfs（`/run/onekvm-firmware`，大小为内存的 80%），
不依赖可能已经损坏的 userdata。文件不能超过这块 tmpfs 的剩余空间。`.fwup` 先由离线 RAUC 校验 CMS 签名，并通过 dm-verity 只读挂载验证载荷与机型，
不在内存中复制整份解压镜像。读取全部载荷校验后再标记目标槽 bad、限长写入、回读 hash、更新配套 boot，最后激活。
原始 `.img` 需要可信来源和发布 hash；只恢复非活动槽，不改分区表或启动链。
Pro 的 JD9853 屏幕显示 Recovery/IP，开机按住旋钮 KEY_ENTER 也可进入。
所有存储动作跟随本次 root 介质；SD 提供整卡 MSC 与 ACM，Pro eMMC 只提供 ACM。
Pro SD 仍需临时 U-Boot 入口，Recovery 不修改或保存 U-Boot 环境。

OneKVM 生成的 `.img` 尾部带 512 字节 metadata/hash trailer。Recovery 在读取完整
镜像时核对 profile、SD/eMMC 类型、payload 长度以及两个 SHA-256；缺少 trailer
或介质不匹配会在任何写盘动作前拒绝。trailer 不是 eMMC rootfs/boot 载荷，不能
把它写入厂商启动区域。

离线 RAUC、gzip squashfs 解包器、完整的 `od`/`dd` 及其动态库统一放在
`recovery.zip` 内的 XZ 工具载荷中，进入 Recovery 后解压到内存。签名包的
外层 squashfs 使用 gzip；根文件系统的压缩方式独立。新的 Cube 镜像使用
128 MiB FAT；Recovery 刷写流程仍兼容升级旧版 32 MiB Cube loader。

initramfs 的模块顺序、启动分支、分区挂载边界和构建入口见
[initramfs 流程引导](docs/initramfs-flow.zh-CN.md)。

Recovery 的镜像与回读摘要统一使用 SHA-256。C906 构建使用 XTheadVector（0.7.1）
指令并行计算四个消息调度，压缩链仍顺序执行，输出标准 SHA-256 摘要；其他目标
使用标量实现。设置 `ONEKVM_SHA256_SCALAR=1` 可强制标量路径做对照。
`--self-test` 覆盖短消息和多块输入，`make test` 还会将 SIMD／标量的分片更新
与 Python hashlib 比较。这项优化作用于 Recovery 的摘要工具和网页服务，
与 GnuPG、OpenSSL、Rust、Go 各自的实现独立。

## 编译

```sh
make
```

目标软件包由 `meta-onekvm` layer 中的配方构建。

## 许可证

MIT，详见 [LICENSE](LICENSE)。
