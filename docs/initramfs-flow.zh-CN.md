# NanoKVM initramfs 与 Recovery 流程

[English](initramfs-flow.md) | 简体中文

本文描述 NanoKVM Cube/PCIe 和 Pro（SD/eMMC）共用实现的实际启动顺序、一次性启动模式、Recovery 的挂载边界，
以及 OpenEmbedded 构建入口。这里的“未挂载”专指 SD 卡块分区；initramfs 自身仍会
挂载 `/proc`、`/sys`、`/dev`、`/run`、configfs 和 tmpfs 等伪文件系统。

## 产物组成

`onekvm-initramfs` 在 Cube/PCIe 上打入 A/B FIT；Pro 为每槽 legacy ramdisk。两者共用：

- NanoKVM SD 的自动安装 layer 额外加入 `/init.d/12-installer`：存在安装 token 时先加载
  内置早期 OLED 状态模块，再运行 RAM 安装器，先于分区迁移与普通 Recovery。
- `/init.d/15-nanokvm-firstboot`：首次启动时建立或修复 A/B 与 userdata 布局。
- `/init.d/20-recovery`：读取一次性启动模式，处理 factory reset 或接管 Recovery。
- `/init.d/89-partlabel`：解析 `PARTLABEL=` 参数。
- `/init.d/90-rootfs`：挂载所选只读 SquashFS 根分区。
- `/init.d/91-overlayroot`：挂载 userdata，建立 overlay 并 `switch_root`。

Recovery 的体积较大的部分不放入 A/B initramfs。网页、OpenSSL、校验和工具、固件安装脚本、
离线 RAUC 验签工具与运行时 OLED/Pro framebuffer 模块位于 boot FAT 的 `recovery.zip`，只有确定进入 Recovery 后才解压到
initramfs 内存根文件系统。
NanoKVM 的早期 OLED 状态模块和自动安装 layer 的 RAM 安装器随 initramfs 内置，
因为它们运行时尚未加载此工具包。

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
       -> 临时只读挂载 本次介质的 boot 分区，读取 nextboot-mode.txt，然后卸载
       -> factory-reset：消费模式，格式化 userdata，返回正常启动流程
       -> recovery：消费模式，加载 recovery.zip，停留在 initramfs
       -> 无模式：追加启动次数；第 10 次进入 Recovery，否则返回正常启动流程
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
3. 开机按住 BOOT（Pro 为旋钮 KEY_ENTER）；
4. `recovery` 模式；
5. 启动重试达到上限；
6. 正常启动。

`onekvm-recovery` 写入 `recovery`，`onekvm-factory-reset` 写入
`factory-reset`。两者都先写临时文件、同步、同目录改名，再次同步，避免留下半写
文件。

## 启动尝试

正常启动前，`20-recovery` 临时读写挂载 boot FAT，先读取 `boot-retry-policy.txt`；
文件缺失或无法识别时继续开启保护，内容为 `disabled`、`false` 或 `0` 时清理旧计数器
并继续正常启动。启用保护时向 `boot_retry_time.txt` 追加一行 `1`，同步后卸载。
第 1–9 次继续启动系统，第 10 次直接进入 Recovery。
计数记录损坏、无法读取或写入时也转入 Recovery。主动进入 Recovery 不增加次数；
恢复出厂设置会开启新一轮计数。Recovery 保留失败记录，成功安装固件、恢复出厂
或在恢复网页选择重启时清除。重启助手在挂载 boot FAT 前先释放 USB mass storage。

正常系统的 `onekvm-boot-success.timer` 等待 `multi-user.target` 和 A/B 健康验证完成，
两分钟后再次确认 OneKVM 与健康验证服务正常，再删除计数文件并同步。提前重启
会保留计数。该机制覆盖 initramfs 开始运行后的故障；ROM、U-Boot 或内核尚未进入
initramfs 时的故障无法写入此文件。

## Recovery 入口与挂载边界

进入 Recovery 时 boot FAT 的生命周期是：

1. 只读挂载解析出的 boot 分区（Cube 为 `/dev/mmcblk0p1`），读取 `nextboot-mode.txt`，立即卸载。
2. 若模式有效，读写挂载该分区并删除模式文件，立即卸载。
   自动回退则短暂读写挂载以记录第 10 次启动，卸载后才加载 Recovery。
3. 只读挂载该分区，将 `recovery.zip` 解压到内存根，立即卸载。
4. 检查 `/proc/mounts`；若本次介质的任一 p1–p4 仍挂载，则拒绝启动
   Recovery 服务。

因此，Recovery 网页、网络、OLED、串口和 USB gadget 开始运行时，不会在本机
常驻挂载 boot、rootfs A、rootfs B 或 userdata，也不会执行后续的 rootfs 与
overlayroot 模块。

需要特别区分“未挂载”和“未暴露”：默认 USB mass-storage gadget 会以可写方式将
本次 SD 原始磁盘导出给 USB 主机。Recovery 本机没有挂载分区，但 USB
主机仍可挂载和修改该磁盘。Pro eMMC 不导出可写磁盘，只提供 ACM。任何需要本机写盘的操作都会先解绑 gadget。

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
| 恢复出厂设置 | 解绑 gadget，不挂载 userdata，直接重新格式化本次介质的 userdata，随后重启 |
| 安装 `.fwup` | 解绑 gadget；离线 RAUC 验签、verity 和机型，标记目标 bad 后限长写未启动 rootfs 槽，短暂读写挂载 boot FAT 更新 FIT、构建号、`recovery.zip` 和 `uEnv.txt` |
| 安装完整 `.img` | 先校验尾部 512 字节 metadata/hash trailer 与当前介质；解绑 gadget；只读 loop 挂载镜像内 boot FAT，限长写非活动 rootfs 及配套 boot，不写分区表/启动链，短暂读写挂载设备 boot FAT |
| 重启 | 解绑 USB mass storage，短暂读写挂载 boot FAT 清除启动重试计数，卸载后重启 |

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

Pro 的 SD/eMMC 布局、构建与临时启动限制见 `onekvm-distro/docs/nanokvm-pro-ab-recovery.md`。
所有写入通过共用 storage-layout resolver 定位 root 介质；未知或冲突布局拒绝写入。
新 userdata 使用 f2fs，已有 ext4/f2fs 保留，A/B 共用 upper/work。

## 恢复网络与一次性选择

正常系统点击 Recovery 后，在弹窗的列表中选择本次复用系统网络（默认）、独立 DHCP
或独立静态 IPv4，再确认重启。没有保存按钮，选择和取消不修改用户配置。
下次打开重新默认选择复用系统网络。独立 DHCP 默认同时启用 `192.168.233.10/24`。
更多网络选项提供接口、MAC 与普通 DNS。进入后，Recovery 网页显示当前 IP 地址；
网络可通过控制台修改，仅影响当前会话。

自动 Recovery 使用 `/boot/recovery-network.json` 中已确认的系统有线 IPv4、有效 MAC、
网关及普通 DNS。Core 启动、正常网络确认及相关配置保存后检查同步；待确认事务期间暂停导出。
内容相同不重写，保留上一份有效 `.json.bak`。旧试验版本的 `recovery.network` 配置不再
决定默认网络。CLI `onekvm-cli request system config apply recovery-network` 只同步系统网络。

`GET /api/system/recovery-network` 返回默认预览；`POST` 仅校验和预览当前选择。
`POST /api/system/recovery` 接收本次选择，验证后将快照写入 `nextboot-mode.txt` 第二行，
第一行仍为 `recovery`。写入使用临时文件、同步、改名和读回校验，已有启动请求拒绝覆盖。
initramfs 在只读读取标记时把有界的第二行复制到 `/run/onekvm-recovery-network.once`，
然后消费整个启动标记。本次快照优先于系统快照，后续启动不再复用它；无效时显示原因并回退。
这不会覆盖 `/boot/recovery-network.json`，不向 userdata 保存选择。

快照版本 1 由 `network` 对象和其紧凑 UTF-8 JSON 的 SHA-256 组成，字段顺序为：
`source/interface/mode/address/gateway/fallback_enabled/fallback_address/mac_address/use_custom_dns/dns/dhcp_client_id`。
MAC 默认沿用系统有效值；系统 DHCP 身份优先复用 networkd Client ID。快照不含账户或 Wi-Fi/VPN 密钥。
解析器限制长度、字段、版本、地址和校验和，不执行 JSON 内容。

全部 boot 读取在 USB MSC 绑定前完成；网络启动、DHCP 和网页操作只访问 RAM，
不挂载 userdata 或已导出的 SD。无有效快照或接口不可用时退回 DHCP + `192.168.233.10/24`。
DHCP bound/renew/deconfig 保留备用地址与手动 DNS，共用锁和代际标识阻止旧回调覆盖新会话配置。
恢复出厂设置清理系统快照和备份。

目前只重建物理有线接口和 IPv4；不复用 IPv6、VLAN、Wi-Fi、VPN。
加密 DNS/DNSSEC 需要明确提供恢复专用普通 DNS，不静默降级。
已有正常系统备用地址（如 `.1`）保持原值；复用静态配置不额外叠加备用地址。

以下控制台命令只影响本次会话：

```sh
onekvm-recovery-network status
onekvm-recovery-network dhcp
onekvm-recovery-network static 10.100.99.107/24 10.100.99.1
onekvm-recovery-network fallback 192.168.233.10/24
```

`GET /network` 提供应用配置及来源，`POST /network` 应用会话配置。
`GET /status` 提供全部实际 IPv4 地址和 `networkSource` / `ipv4Mode`。
串口按 DHCP/STATIC/FALLBACK 标记地址；单色 OLED 顶部固定显示 `RECOVERY`，
下方循环显示全部 IP 地址。Pro Desk 使用与初始化首屏一致的彩色静态布局：
OneKVM 标志、同时显示的全部 IPv4 地址及蓝色 `Recovery mode` 状态条。
仅在网络状态变化时重绘，不再周期性清屏。网页定时刷新显示的 IP 地址。
