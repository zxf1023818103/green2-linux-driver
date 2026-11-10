# Green2 USB Bluetooth integration

此处直接包含已加入 Green2 例外规则的 btusb.c 及其四个内核私有头文件。
来源为 Ubuntu linux-source-7.0.0 7.0.0-38.38，原始文件摘要见 UPSTREAM.json。

固定固件的 0bda:f851 蓝牙接口（class 0xe0）在 Realtek 通配项之前匹配，
不设置 BTUSB_REALTEK，从而使用通用 HCI USB 路径。没有固件模式判断，
也不需要外置 patch 或运行 patch 命令。Wi-Fi 接口继续由 WHC_USB 管理。

```sh
make -C driver/platforms/linux_android/bluetooth \
  KDIR=/path/to/prepared/7.0.0-38-generic CC=gcc-15 -j4
```

生成 btusb.ko，使用目标内核现有的 btintel/btbcm/btrtl/btmtk 依赖模块。
它与 WHC 模块分别构建，Core 26 构建脚本会将其安装进 kernel snap。
当前 Android 使用板载 UART 蓝牙 HAL，没有构建或加载此模块。
本目录固定于上述 Ubuntu 内核；接入其他内核时应在该内核对应的 btusb 源码中
保留相同的 Green2 匹配规则，不将 Linux 7.0 的 btusb.ko 跨内核直接复用。
