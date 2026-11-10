# Green2 Wi-Fi project

AmebaGreen2 的 Linux / Android 共用驱动、协议核心、CLI 与 Android 无屏服务。
仓库按五层组织：

```text
shared/       协议头文件与纯 C 算法
driver/       内核驱动、USB/SDIO 总线、Linux/Android 接入
cli_tool/     CMake CLI/PASN 库、Linux 服务、Ubuntu Core 26 镜像集成
android_app/  Gradle + NDK CMake APK、独立 AIDL HAL、Android 镜像打包
docs/         架构、构建、调试和回归说明
```

- [架构与 HAL 边界](docs/architecture.md)
- [构建指南](docs/build_guide.md)
- [调试指南](docs/debugging.md)
- [Android 板级集成](docs/board_integration.md)
- [实测结果和未解决问题](VALIDATION.json)

APK 保留包名 cn.zenghome.flashstudio，经 WifiAwareManager 使用系统 Wi-Fi 栈。
Windows 接入目录目前为移植说明，尚无 WiFiCx/NetAdapterCx 实现。

源码来源于 [ameba-rtos 96129dc4a](https://github.com/zxf1023818103/ameba-rtos/commit/96129dc4a88ea090cad89057f01251fad8dbcc8a)。
[UPSTREAM.json](UPSTREAM.json) 保留导入路径与原始摘要；每个文件保留其原许可证声明，
见 [LICENSE](LICENSE) 和 [LICENSES](LICENSES)。外部依赖见 [DEPENDENCIES.json](DEPENDENCIES.json)。

当前开发回归使用预编译工具链/主机工具。固件、编译产物、个人配置、运行日志、
配对缓存和签名私钥不进入 Git。构建、系统启动与真实无线测试分别记录，避免
将目录重排或编译通过等同于设备回归完成。
