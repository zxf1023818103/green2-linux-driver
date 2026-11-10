# Debugging

## Android

先确认 ADB serial、板型、内核和 USB VID/PID，再操作设备：

```sh
adb devices -l
adb shell uname -r
adb shell dumpsys wifiaware
adb shell dumpsys activity service cn.zenghome.flashstudio/.Green2Service
adb shell ip -6 neigh show
adb shell ss -lnt
adb logcat -s Green2WifiHAL Green2PASN Green2Services
adb shell dmesg
```

排查顺序：发布 → PIN 握手 → Android 配对确认 → 缓存 NIRA 验证 →
NDP 确认 → IPv6 邻居 → TCP SYN/ACK → 应用协议。手机显示配对成功
不足以证明 Android 已收到确认；接收到 SYN 也不足以证明回包路径正确。

抓包时区分 NDP 尚未建立、邻居查询无回复、RST、应用超时和实际有效载荷。
记录测试窗口起始 uptime，只统计该窗口内新增的驱动错误；保留旧错误的来源。

## Ubuntu Core 26 / 双机

使用新测试 VM、独立磁盘和 SSH key。先验证首次启动、snap seeding、
模块 ABI、服务及权限，再接通 Green2 原生 USB。日志用 journalctl、dmesg、
服务计数和 socket/邻居状态交叉确认。PASN 原始事件和 NIK/NPK 缓存不公开。

VMware USB 直通前检查 0bda:f851 对应的实际设备与接口，串口单独保留。
若与 Android 板共用一个模组，应先结束一侧测试再换线，避免并发接管。

## Windows / WinDbg

当前 Windows 负责 VMware USB 直通、虚拟机管理及 COM 串口。
使用 Get-PnpDevice、VMware 状态和串口日志检查这些链路。
Linux guest 的内核模块错误通过 guest dmesg/串口调试。

本仓库尚无 WiFiCx/NetAdapterCx 驱动，因此没有可加载到 Windows 的 .sys、
测试签名或 WinDbg 驱动回归。未来 Windows 移植须配套 WDK 构建、符号和
独立测试机，才能开展 WinDbg/KD 调试。
