# Services and deployment configuration

- common/：Linux NAN/BLE 服务脚本、TCP 吞吐服务及发布配置。
- config/hostap-pasn.config：构建外部 hostap libpasn.so 使用的固定配置。
- ubuntu-core26/：Core 26 snap、镜像构建、启动包装脚本和运行时依赖集成。

CLI 源码、头文件、测试和 CMake 构建入口统一在 ../cli_tool/。
协议和纯 C 算法继续共用于 ../shared/。服务镜像引用这些正式构建产物。
