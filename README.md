# Green2 Linux driver

AmebaGreen2 / RTL8711F 的独立 Linux WHC 驱动及 NAN/PASN 公共库。
普通 Linux 服务与 Android Wi-Fi HAL 可以引用同一份驱动和协议核心。

来源为 [ameba-rtos 的 `96129dc4a`](https://github.com/zxf1023818103/ameba-rtos/commit/96129dc4a88ea090cad89057f01251fad8dbcc8a)。
导入路径、SDK 依赖文件和原始 SHA-256 见 [UPSTREAM.json](UPSTREAM.json)。
每个文件保留其版权和许可证声明，参见 [LICENSE](LICENSE) 与 [LICENSES](LICENSES)。

## 目录

```text
green2-linux-driver/
├── src/
│   ├── core.c                # USB 初始化、probe、disconnect、注册
│   ├── hw_ops.c              # USB 收发与传输操作
│   ├── internal.h            # 内部私有声明
│   ├── netdev/               # cfg80211 / 网络接口
│   ├── usb/                  # USB 私有头文件与构建片段
│   └── ...                   # WHC、其他总线及原有子模块
├── include/
│   ├── my_driver.h           # 共用硬件标识
│   ├── green2_pasn.h         # 共用 PASN / 缓存 C API
│   └── sdk/                  # 随仓库提供的 SDK 依赖头文件
├── Kbuild
├── Makefile
├── Android.bp                # Soong 根包源码/头文件导出
├── android/
│   ├── Android.bp
│   └── board_integration.md
├── scripts/
│   ├── build_linux.sh
│   └── build_android.sh
├── app/                      # 公共 NAN/PASN 实现及 Linux 工具
├── configs/
├── tests/
└── README.md
```

Android AIDL/HAL、Java 服务、SELinux 策略及 YM6252 镜像打包由 Android 工程维护。
仓库不包含固件镜像、工具链、编译产物、运行日志或设备配对缓存。
USB、SDIO、SPI 等原有源码仍保留；本轮以 Green2 USB NAN 为验证目标。

## 构建内核模块

使用目标内核的已准备构建目录、配置、导出符号及相应的预编译工具链：

```sh
make amebagreen2_usb_nan_bulk2048_defconfig
make prepare
make KDIR=/path/to/prepared-kernel-build ARCH=arm64 LLVM=1
```

产物为 `whc_usb.ko`。`make prepare` 检查已收纳的 SDK 头文件，
不下载文件或访问原 RTOS 仓库。交互式 `make menuconfig` 需要外部 kconfiglib；
直接选择 defconfig 不需要它。

Android GKI 需使用该内核对应的模块 ABI、Module.symvers 和配置。
如 cfg80211 是外部模块，额外传入它的 `KBUILD_EXTRA_SYMBOLS`。
Android 和普通 Linux 共用源码，各自为目标内核编译 `.ko`。

## 构建公共库与 Linux CLI

公共库依赖与 Green2 NAN 配套的 hostap PASN 源码/库、OpenSSL 和 libnl。
hostap 版本及导出接口应与调用方匹配；它们不是本仓库内置的工具链或固件。
Android 工程当前使用的 hostap 来源见 [DEPENDENCIES.json](DEPENDENCIES.json)。

```sh
make -C app/wifi_nan_utility/pasn_probe HOSTAP_DIR=/path/to/hostap-pasn libgreen2pasn.so pasn_live
make -C tests HOSTAP_DIR=/path/to/hostap-pasn run
```

将公共库、`libpasn.so` 和 CLI 放在运行时可解析的库路径中。
CLI 保留原有接口：

```sh
export GREEN2_NPK_FILE=/var/lib/nan/pasn_npk
export GREEN2_PASN_RESULT_FILE=/run/nan/pasn-result
./pasn_vendor_events nan0 | ./pasn_live nan0 2437
```

上面是调用接口示例，NAN 接口、配对流程、目录权限及服务管理仍由平台负责。
`pasn_vendor_events` 输出包含握手材料，应直接送入 CLI，勿作为公开日志保存。

## 公共 API 与平台边界

头文件为 `include/green2_pasn.h`。
`green2_pasn_create_config()` 接收接口、频率、缓存路径、存储回调、日志回调
和密钥安装事件回调。公共核心不写死 Android 或 Linux 的数据目录，也不读取进程环境变量。

- 默认文件存储使用 0600 临时文件、fsync 和 rename；也可由平台提供完整读写回调。
- 首次配对只选择本次握手中变化且唯一的缓存记录，缓存验证仍检查 NIRA。
- 当前绑定的 hostap eloop/crypto 使用进程级状态：每进程一个引擎，调用方串行调用。
- Android 适配层指定 `/data/vendor/wifi/green2` 并映射 AIDL 事件；Linux CLI 指定自身路径。
- IPv6 邻居管理、接口生命周期和 TCP 监听时机由平台接入层负责，未迁入驱动。

## 修复与验证范围

本仓库收纳 `netif_rx()` 后读取已释放 skb 的修复、NAN 命令错误传递、
参数与内存分配检查，以及 Android 实测后的缓存身份选择逻辑。

公共核心的 ARM 测试已在 YM6252 Android 板上运行，覆盖 SAE H2E、NIRA 完整性、
多记录身份选择、失败处理、原子文件存储和握手重传。Linux CLI 适配器也已交叉编译并完成无输入启动/退出测试。
普通 Linux/glibc 的原生运行、其他内核及总线仍需分别验证。

提取前相同驱动修复及协议逻辑已通过 iPhone NAN 配对、缓存验证、TCP PING/PONG
和每方向 10 秒的双向吞吐；上传接收端约 26.18 Mbps。
公共库提取后尚未替换正在运行的板端 HAL，也未刷写系统；不能据此声称提取后完整空口回归已完成。

构建和测试状态见 [VALIDATION.json](VALIDATION.json)。Android 接入步骤见 [android/board_integration.md](android/board_integration.md)。
