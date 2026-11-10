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

## 本轮设备回归边界

Android 镜像完成真实 iPhone 双向吞吐：手机报告上行 24 Mbps、下行 30 Mbps。
这两个值是手机显示值；NAN 接口累计流量含协议开销，不能替代有效吞吐统计。
完整检查状态和产物摘要见根目录 VALIDATION.json。

已知内核警告：固件异步 WHC_API_CFG80211_NAN_CFGVENDOR_CMD_REPLY 经工作队列
调用 cfg80211_vendor_cmd_alloc_reply_skb 时，原 nl80211 请求上下文可能已结束。
冷启动日志出现 __cfg80211_alloc_reply_skb WARN；功能测试通过不代表该问题已修复。
这是公共 Linux 驱动的回复生命周期问题，不能当作 Android 专属行为或屏蔽日志处理。

在两个 USB 主机之间移动正在运行的模组后，本轮首次 Core 26 探测出现 API 0x7
超时；通过已连接串口的 RTS 复位模组后恢复。先停旧主机服务、完成 USB 所有权
转移，再检查固件就绪和目标主机枚举，必要时复位模组。不要把未枚举或未真正发布
的服务记为无线测试通过。

Core 26 手机实测：上行 19.019 Mbps、下行 25.881 Mbps；NAN 接口收发错误和
丢包计数均为 0。板端上行按接收字节计为 19.0189346 Mbps，下行按提交字节计为
26.1774497 Mbps；下行发送/接收统计口径与计时窗口不同，报告分别保留。
本轮 Core 26 使用默认 2.4 GHz 配置，Android 允许 2.4/5 GHz，因此该表只能表示
各自功能回归结果，不能用于判断两个平台的性能差异。Core 26 验证了真实蓝牙
控制器和广播；完整 BLE GATT 读写/通知回归在 Android 上完成。
