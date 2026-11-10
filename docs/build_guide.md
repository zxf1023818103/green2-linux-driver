# Build guide

使用预编译编译器、SDK/NDK、CMake、Gradle 和镜像工具。Linux/Android 共用
源码，各自生成匹配目标内核/用户态 ABI 的产物，不交叉复用 .ko 或 libc 二进制。

## Driver

```sh
make -C driver amebagreen2_usb_nan_bulk2048_defconfig
KDIR=/path/to/prepared/kernel/build driver/scripts/build_linux.sh ARCH=arm64 LLVM=1
```

Android 使用准备好的 GKI 构建目录及预编译 LLVM：

```sh
KERNEL_SRC=/path/to/android/kernel/source \
KERNEL_OUT=/path/to/prepared/kernel/output \
ANDROID_CLANG_BIN=/path/to/prebuilt/llvm/bin \
driver/scripts/build_android.sh -j2
```

产物：driver/whc_usb.ko。内核配置、导出符号、签名和外部 cfg80211 的
Module.symvers 必须与目标一致。脚本不构建工具链或完整 AOSP。

## CLI / PASN

准备 DEPENDENCIES.json 中固定的 hostap PASN 源码/库，以及 OpenSSL、libnl。

```sh
cmake -S cli_tool -B /path/to/cli-build \
  -DGREEN2_HOSTAP_DIR=/path/to/hostap \
  -DGREEN2_BUILD_LEGACY_NAN_TOOLS=ON
cmake --build /path/to/cli-build --parallel 4
ctest --test-dir /path/to/cli-build --output-on-failure
```

主要产物：green2_cli、pasn_live、pasn_vendor_events、libgreen2pasn.so。
发布服务兼容工具为 rtw_aware / libnanapi.so；原厂其他诊断源码保留于 legacy/。
CLI 的两种入口：`green2_cli events nan0`、`green2_cli pasn nan0 2437`。
事件流含握手材料，应直接通过管道送入 PASN 处理，不作为公开日志保存。

Android 交叉编译使用 cli_tool/build_android.sh。设置 ANDROID_NDK_HOME、
GREEN2_HOSTAP_DIR、GREEN2_PASN_LIBRARY、GREEN2_LIBNL_LIBRARY、
GREEN2_LIBNL_INCLUDE_DIR、GREEN2_CRYPTO_LIBRARY。Android HAL 可通过
GREEN2_ANDROID_ADAPTER_SOURCE 加入自身的路径和日志适配。

## APK / JNI

使用 JDK 17+、Gradle 8.11.1、AGP 8.10.1、SDK/Build Tools 36 和
NDK 26.2.11394342。AGP 8.10 支持 API 36，兼容版本参见
[Android 官方说明](https://developer.android.com/build/releases/agp-8-10-0-release-notes)。

```sh
ANDROID_HOME=/path/to/android-sdk \
GREEN2_SRT_AAR=/path/to/srtdroid-core-1.9.5.aar \
GREEN2_SRT_INCLUDE=/path/to/srt-include \
GRADLE_BIN=/path/to/gradle-8.11.1/bin/gradle \
android_app/build_android.sh
```

如使用 PATH 中的预编译 CMake，可通过 -Pgreen2CmakeVersion 指定其版本。
输出位于 android_app/app/build/outputs/apk/release/。release APK 默认未签名；
这是使用 android.uid.system 的板端服务，部署前须使用目标系统的平台证书签名。
私钥、keystore、SDK 路径配置和 AAR 不进入仓库。

JNI 在 app/src/main/cpp/，通过 CMake 编译 green2services 并链接外部预编译
SRT 库。Java 在 app/src/main/java/cn/zenghome/flashstudio/。
HAL 在 android_app/hal/，需要设备匹配的生成 AIDL 头文件及 vendor 依赖，
不随 APK 编译为 JNI。

## Images

- Android：android_app/images/package_image.py 使用调用方提供的合法基线镜像、
  HAL、库、模块、APK 和签名输入，生成 AVB 校验的 vendor/super 候选；
  verify_image.py 做 LP 布局与逻辑镜像回读比对。脚本本身不刷机。
- Ubuntu Core 26：cli_tool/integration/ubuntu-core26/，以固定 Canonical snap、
  hostap 和内核版本构建开发镜像；SSH 公钥是本地输入，不提交镜像或个人配置。

完整设备测试使用构建产物的 SHA-256 区分旧运行版本、候选镜像和实际启动版本。
