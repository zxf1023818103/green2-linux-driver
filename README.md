# Green2 Wi-Fi project

AmebaGreen2 的 Linux / Android 共用驱动、协议核心、CLI 与 Android 无屏服务。
仓库按模块组织：

```text
shared/       协议头文件与纯 C 算法
driver/       展平的 Linux/Android 内核源码、头文件与模块构建入口
cli_tool/     展平的 CLI/PASN/NAN 源码、头文件、测试和 CMake 构建
services/     Linux 服务、配置和 Ubuntu Core 26 镜像集成
android_app/  Gradle + NDK CMake APK、独立 AIDL HAL、Android 镜像打包
docs/         架构、构建、调试和回归说明
```

- [架构与 HAL 边界](docs/architecture.md)
- [构建指南](docs/build_guide.md)
- [调试指南](docs/debugging.md)
- [Android 板级集成](docs/board_integration.md)
- [实测结果和未解决问题](VALIDATION.json)
- [USB 换主机调查](docs/usb_handoff.md)

APK 保留包名 cn.zenghome.flashstudio，经 WifiAwareManager 使用系统 Wi-Fi 栈。
Windows 接入目录目前为移植说明，尚无 WiFiCx/NetAdapterCx 实现。

源码来源于 [ameba-rtos 96129dc4a](https://github.com/zxf1023818103/ameba-rtos/commit/96129dc4a88ea090cad89057f01251fad8dbcc8a)。
[UPSTREAM.json](UPSTREAM.json) 保留导入路径与原始摘要；每个文件保留其原许可证声明，
见 [LICENSE](LICENSE) 和 [LICENSES](LICENSES)。外部依赖见 [DEPENDENCIES.json](DEPENDENCIES.json)。

当前开发回归使用预编译工具链/主机工具。固件、编译产物、个人配置、运行日志、
配对缓存和签名私钥不进入 Git。构建、系统启动与真实无线测试分别记录，避免
将目录重排或编译通过等同于设备回归完成。

CLI 自有源码、头文件和测试全部放在 cli_tool/ 根目录。服务脚本在 services/common/，
构建配置在 services/config/，Core 26 集成在 services/ubuntu-core26/。
公共协议及纯 C 算法继续位于 shared/，由 CLI 和 Android HAL 共用。

Linux/Android 驱动的 C/H 文件位于 driver/ 同一层，构建片段、defconfig 和设备树
资料在 driver/configs/，构建脚本在 driver/scripts/。Windows 目录保持原状。
