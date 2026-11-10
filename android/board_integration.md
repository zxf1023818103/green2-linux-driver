# Android 板级集成

Linux 与 Android 都通过根目录 Kbuild 编译同一份内核代码。
Android.bp 导出源码和用户态公共头文件；它不会把内核驱动当成 Soong C/C++ 库编译。
根目录 Android.bp 声明源码文件组，android/Android.bp 提供设备侧别名与 PASN 编译默认项，
避免在 Soong 子包中引用不允许的 `../src` 路径。

## 使用预编译 LLVM 和已准备的 GKI 构建目录

```sh
KERNEL_SRC=/path/to/android-kernel-source \
KERNEL_OUT=/path/to/prepared/android-kernel-out \
ANDROID_CLANG_BIN=/path/to/prebuilt/llvm/bin \
./scripts/build_android.sh -j2
```

脚本只调用外部模块构建，不构建 AOSP、内核镜像或工具链。内核输出必须已有
配置、generated headers、host build tools 与匹配的 Module.symvers。
外置 cfg80211 时再传入 `KBUILD_EXTRA_SYMBOLS=/path/to/cfg80211.symvers`。
产品需要的模块签名、GKI 符号限制及 ABI 配置由对应内核发行方决定。
产物为根目录 whc_usb.ko；不要复用其他内核生成的 .ko。

## Kleaf / Bazel

在设备现有 Kleaf workspace 中把本仓库声明为外部 kernel_module 的源码，
使用本仓库根 Makefile/Kbuild，并把 whc_usb.ko 列入 outs。
kernel_build 应指向设备实际使用的内核目标；外部 cfg80211 使用对应模块依赖。
规则导入路径、Bzlmod 设置和目标标签应遵循该设备固定的 Kleaf 版本。
本仓库未包含独立 AOSP/Kleaf workspace，未宣称完整 Soong/Kleaf 构建已通过。

参考：[Kleaf 外部模块说明](https://android.googlesource.com/kernel/build/+/refs/heads/main/kleaf/docs/impl.md)。

## 安装和加载

在产品的 BoardConfig.mk 中，把已构建模块加入 BOARD_VENDOR_KERNEL_MODULES，
由 Android 构建生成模块依赖元数据；分区选择由产品配置决定。
Green2 非启动存储依赖，通常无需放入 first-stage vendor ramdisk。
加载时满足 rfkill/cfg80211 依赖，再由产品模块加载列表或 init 服务加载 whc_usb。

参考：[AOSP 模块安装与加载](https://source.android.com/docs/core/architecture/kernel/kernel-module-support)。

## Device Tree 与 USB

Green2 USB IOT 模式为 VID 0x0bda、PID 0xf851，Wi-Fi 使用复合 USB 设备的接口 2。
USB 驱动按设备 ID 枚举，无需为了驱动添加虚构的设备树 compatible。
板级 DTS 仍需正确配置 USB host、供电、复位和连接拓扑；GPIO 不在公共驱动中硬编码。
SDIO 板级配置应按实际接线单独处理，本轮未验证。

## Android Wi-Fi Aware 用户态

仅加载模块不会自动提供 WifiAwareManager。设备需另行提供 Wi-Fi AIDL HAL、
服务与能力声明、SELinux 策略及应用接入。
公共 PASN 头文件为 include/green2_pasn.h；Android 适配层提供缓存路径与日志回调。
原应用网络生命周期、IPv6 邻居维护和提前 TCP 监听逻辑继续由平台接入层管理。
本轮公共库提取未替换当前运行的板端 HAL，也没有刷机。
