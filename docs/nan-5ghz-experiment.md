# Core26 / Green2 强制 5 GHz 验证

两个仓库均使用 `experiment/nan-5ghz-only` 实验分支，默认生产配置不变。
需要 ameba-rtos 同名分支的 `NAN_5G_ONLY=ON` 固件。

## 内核与用户态

使用 Ubuntu `linux-source-7.0.0=7.0.0-38.38` 及同版本预编译 headers，仅构建 cfg80211 模块：

```sh
bash driver/scripts/build-core26-nan5g.sh /path/to/new-kernel-work
```

在原 Core26 builder 中执行；不重编译主机编译器或整个系统镜像。
内核补丁增加默认关闭的 `nan_allow_5g_only` 模块参数：

- opt-in 后允许 NAN 的 `bands=2`。
- 显式仅 5GHz 时，不启用 2.4GHz DW，也不强制校验 2437 MHz 信道 6。
- 5GHz 的区域及信道校验仍保留。
- 参数默认关闭，普通 2.4GHz / 双频沿用原检查。

先停止 NAN，关闭 NAN netdev，再卸载 whc_usb / cfg80211，加载实验模块：

```sh
insmod /path/to/cfg80211-nan5g.ko nan_allow_5g_only=1
modprobe whc_usb
```

仅在没有其他无线网卡依赖 cfg80211 的隔离测试机上切换模块。
现有 Core26 镜像测试使用独立 transient service 与临时脚本覆盖，不改原 Snap，
原配对缓存保留。5GHz 模式准备脚本要求模块已 opt-in 且 PHY 全部 2.4GHz 信道禁用。
NAN 请求只带 5GHz，PASN 应答频率为 5745 MHz。

## 验证结果

2026-11-12：模块实际加载、NAN 驱动回调 bands=2；设备固件确认
mask=2、primary=149、secondary=149、enabled=1。PHY 只允许 36/40/44/48/149/153/157/161/165。
Publisher 命令成功、TCP 8000 监听，但用户报告服务不可见，并说明已删除配对；系统添加设备列表也不可见。
保留配对状态下的互通尚未完成独立验证。
无手机配对请求进入主机，因此未完成纯 5GHz 的 PIN/PASN/NDP/TCP/吞吐互通。
仅配置和启动通过，不能把它描述为纯 5GHz NAN 可用。

恢复时停止实验服务，刷回保存的原 app.bin，卸载实验 cfg80211 后用 modprobe 加载原模块，
再启动原 Snap NAN / BLE 服务（双频、CN）。本次未更换系统镜像、未写 eFuse。

双频对照：刷回原 app.bin、加载原 cfg80211、恢复双频 CN 后，同一手机重新看到设备并收到 PONG。对照完成后，按用户要求重新切回纯 5GHz 实验状态；首次发现仍未通过。
