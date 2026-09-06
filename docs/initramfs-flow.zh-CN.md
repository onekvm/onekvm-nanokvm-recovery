# NanoKVM initramfs 与 Recovery 流程

[English](initramfs-flow.md) | 简体中文

本文描述 NanoKVM 当前实现的实际启动顺序、一次性启动模式、Recovery 的挂载边界，
以及 OpenEmbedded 构建入口。这里的“未挂载”专指 SD 卡块分区；initramfs 自身仍会
挂载 `/proc`、`/sys`、`/dev`、`/run`、configfs 和 tmpfs 等伪文件系统。

## 产物组成

`onekvm-initramfs` 被打进 A/B 两个 FIT 启动镜像。NanoKVM 额外安装：

- `/init.d/15-nanokvm-firstboot`：首次启动时建立或修复 A/B 与 userdata 布局。
- `/init.d/20-recovery`：读取一次性启动模式，处理 factory reset 或接管 Recovery。
- `/init.d/89-partlabel`：解析 `PARTLABEL=` 参数。
- `/init.d/90-rootfs`：挂载所选只读 SquashFS 根分区。
- `/init.d/91-overlayroot`：挂载 userdata，建立 overlay 并 `switch_root`。

Recovery 的体积较大的部分不放入 FIT。网页、OpenSSL、校验和工具、安装脚本及
OLED 模块位于 boot FAT 的 `recovery.zip`，只有确定进入 Recovery 后才解压到
initramfs 内存根文件系统。

## Recovery Web UI 从哪里来

Recovery Web UI 的源码在相邻的 `onekvm-ui` 仓库，不在本仓库：

- `onekvm-ui/recovery.html`：独立 HTML 入口；
- `onekvm-ui/src/recovery/main.ts`：Vue 启动入口；
- `onekvm-ui/src/recovery/App.vue`：Recovery 页面；
- `onekvm-ui/src/recovery/api.ts`：`/status`、`/progress`、`/firmware`、
  `/reset-user`、`/factory-reset` 和 `/reboot` 客户端；
- `onekvm-ui/src/recovery/messages.ts`：简中、繁中和英文文案。

它与正常控制台是同一个源码仓库中的第二套 Vite 构建，但使用独立配置
`onekvm-ui/vite.recovery.config.ts`。该配置禁用 `public/`，使用单入口和
`inlineDynamicImports`，并在构建结束后把生成的 JavaScript 和 CSS 全部内联到：

```text
onekvm-ui/dist-recovery/index.html
```

构建插件还会拒绝残留的 `/assets/` 引用，并检查最终 HTML 是否包含固件、重置
用户、恢复出厂和重启 API。最终页面因此是一个自包含 HTML；Recovery 环境不需要
Node.js，也不依赖正常系统的 `/usr/share/onekvm-ui`。

执行 `onekvm-distro/scripts/kas.sh` 时，在 kas 容器启动前会先调用
`scripts/oe-ui-artifact.sh`：

1. 要求宿主使用 Node.js 22.12 或更新的 22.x，并按 lockfile 执行
   `pnpm install --frozen-lockfile` 或 `npm ci`。
2. `pnpm run build`（或 npm 项目的 `npm run build`）同时生成正常 UI 的
   `dist/` 和 Recovery 的 `dist-recovery/`。
3. 将 Recovery 输出以固定时间戳、uid/gid、文件顺序和 `gzip -n` 打包为可复现
   artifact：

   ```text
   onekvm-distro/.oe/artifacts/onekvm-ui-recovery-dist.tar.gz
   ```

4. 源文件、版本、edition 和 Node/包管理器版本的输入 SHA-256 未变化时复用缓存；
   任一输入变化都会重新构建。Pro edition 会先叠加 `onekvm-pro-ui/src/product`，再
   走同一套 Recovery 构建。

`onekvm-recovery_1.0.bb` 从 `/work/artifacts/onekvm-ui-recovery-dist.tar.gz`
取出 `index.html`，安装到 Recovery payload 的：

```text
/usr/share/onekvm-recovery/index.html
```

同一 payload 还包含 C Web 服务器、TLS 证书和私钥、OpenSSL 库、刷写/校验工具及
OLED 模块，配方最终把它们一起生成 `onekvm-recovery.zip`。SD installer 把 zip
放入 boot FAT；RAUC `.fwup` 也携带它，bundle hook 或 Recovery 刷写脚本会更新
`/boot/recovery.zip`。

进入 Recovery 后，`20-recovery` 将 zip 解压到 initramfs 的 `/`。C 程序
`onekvm-recovery-web` 默认从 `/usr/share/onekvm-recovery` 提供静态文件，在 80
端口提供 HTTP、在 443 端口尽可能提供 HTTPS，并在同一进程处理上述同源 API。

因此，修改 Recovery UI 后，仅执行 `onekvm-ui` 的本地 Vite 构建只能用于检查
`dist-recovery/index.html`；要让设备实际使用新页面，仍须通过
`onekvm-distro`/kas 重建 `recovery.zip` 所属的 SD 镜像或 `.fwup`，再按授权流程
部署。禁止把 `dist-recovery/` 或 `.oe/` 产物提交进 Git。

## `/init` 与模块顺序

OpenEmbedded 的 `initramfs-framework` `/init` 依次完成：

1. 挂载 procfs、sysfs、`/run` tmpfs 和 devtmpfs。
2. 将内核命令行转换为 `bootparam_*` shell 变量。
3. 按 `/init.d/` 文件名顺序执行启用的模块。

在已经完成 A/B 初始化的设备上，关键路径是：

```text
/init
  -> 15-nanokvm-firstboot（确认布局完整后返回）
  -> 20-recovery
       -> 临时只读挂载 mmcblk0p1，读取 nextboot-mode.txt，然后卸载
       -> factory-reset：消费模式，格式化 userdata，返回正常启动流程
       -> recovery：消费模式，加载 recovery.zip，停留在 initramfs
       -> 无模式：返回正常启动流程
  -> 89-partlabel
  -> 90-rootfs（挂载当前 A/B SquashFS）
  -> 91-overlayroot（挂载 userdata 并 switch_root）
```

`15-nanokvm-firstboot` 早于 Recovery hook。若磁盘仍是首次安装的两分区布局，
它可能重写分区表、写入 A/B、创建 userdata，并短暂挂载分区做校验；因此“Recovery
不挂载根分区”的保证适用于已经完成初始化的正常 A/B 布局，不覆盖首次迁移过程。

## 一次性启动模式

正常系统通过 `/boot/nextboot-mode.txt` 请求下次启动行为。只读取第一行，支持：

```text
recovery
factory-reset
```

写入工具固定使用 LF。读取端会删除第一行中的 CR，所以以下 Windows CRLF 内容也
有效：

```text
recovery\r\n
```

未知值不会触发任何模式，也不会被自动删除。有效值在动作开始前通过短暂读写挂载
boot FAT 删除；如果消费失败，initramfs 会停止，不会带着仍有效的模式继续执行。

触发优先级如下：

1. `factory-reset` 模式；
2. 内核命令行 `onekvm_recovery=1`；
3. 开机按住 BOOT；
4. `recovery` 模式；
5. 正常启动。

`onekvm-recovery` 写入 `recovery`，`onekvm-factory-reset` 写入
`factory-reset`。两者都先写临时文件、同步、同目录改名，再次同步，避免留下半写
文件。

## Recovery 入口与挂载边界

进入 Recovery 时 boot FAT 的生命周期是：

1. 只读挂载 `/dev/mmcblk0p1`，读取 `nextboot-mode.txt`，立即卸载。
2. 若模式有效，读写挂载该分区并删除模式文件，立即卸载。
3. 只读挂载该分区，将 `recovery.zip` 解压到内存根，立即卸载。
4. 检查 `/proc/mounts`；若任一 `/dev/mmcblk0p1` 至 `p4` 仍挂载，则拒绝启动
   Recovery 服务。

因此，Recovery 网页、网络、OLED、串口和 USB gadget 开始运行时，不会在本机
常驻挂载 boot、rootfs A、rootfs B 或 userdata，也不会执行后续的 rootfs 与
overlayroot 模块。

需要特别区分“未挂载”和“未暴露”：默认 USB mass-storage gadget 会以可写方式将
整个 `/dev/mmcblk0` 原始磁盘导出给 USB 主机。Recovery 本机没有挂载分区，但 USB
主机仍可挂载和修改该磁盘。任何需要本机写盘的操作都会先解绑 gadget。

Recovery 空闲状态仍会挂载以下非 SD 文件系统：

| 挂载 | 用途 |
| --- | --- |
| procfs、sysfs、devtmpfs | initramfs 基础运行环境 |
| `/run` tmpfs | 运行时状态 |
| configfs | 创建 USB mass-storage 与 CDC ACM gadget |
| `/run/onekvm-firmware` tmpfs | 固件上传，默认上限为内存的 80% |

## Recovery 操作对存储的影响

| 操作 | 分区行为 |
| --- | --- |
| 仅进入/空闲 | boot FAT 只在入口阶段短暂挂载；服务启动后无 SD 分区挂载 |
| 重置默认 Web 用户 | 解绑 USB gadget，读写挂载 userdata；当前实现会保持挂载到重启 |
| 恢复出厂设置 | 解绑 gadget，不挂载 userdata，直接重新格式化 `mmcblk0p4`，随后重启 |
| 安装 `.fwup` | 解绑 gadget；直接写未启动 rootfs 槽，短暂读写挂载 boot FAT 更新 FIT、构建号、`recovery.zip` 和 `uEnv.txt` |
| 安装完整 `.img` | 解绑 gadget；只读 loop 挂载镜像内 boot FAT，直接写 rootfs A，短暂读写挂载设备 boot FAT |
| 重启 | 启动模式已在入口消费，不再为了清标记挂载 boot FAT |

## 构建与验证

目标 initramfs、FIT、`recovery.zip`、SD 镜像和 bundle 必须由
`onekvm-distro` 的 OpenEmbedded/kas 流程生成，不能树外交叉编译或手工组装：

```sh
cd onekvm-distro
make image sd
```

完整 NanoKVM 产物使用：

```sh
cd onekvm-distro
make firmware
make image sd
```

相关配方为：

- `meta-onekvm/recipes-core/images/onekvm-initramfs.bb`
- `meta-onekvm-bsp/recipes-support/onekvm-recovery/onekvm-recovery_1.0.bb`
- `meta-onekvm-bsp/recipes-bsp/onekvm-boot/onekvm-boot-image.bb`

仓库内的 `make test` 只运行宿主机单元测试和辅助程序自检，不生成可部署的目标产物。
OpenEmbedded 产物位于 `onekvm-distro/.oe/`，不进入 Git。
