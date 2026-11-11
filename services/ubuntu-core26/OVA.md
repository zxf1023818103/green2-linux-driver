# Ubuntu Core 26 VMware OVA

包含 Green2 WHC / btusb 内核模块，以及 NAN、BLE、TCP 和 SRT 服务。目标为 VMware amd64 虚拟机，默认 2 vCPU、2 GiB 内存、16 GiB 薄置备磁盘、NAT 网络。无需编译主机工具。

## 配置用户名和密码

镜像没有默认登录密码，也不包含构建者的 SSH 公钥。首次启动从 VMware GuestInfo / OVF 属性创建本地管理员，支持 SSH 密码登录和需要密码的 sudo；root 禁止 SSH 登录。

VMware Workstation 请把 `Green2-Core26.ova` 和 `Import-Core26.ps1` 放在同一目录，用 PowerShell 执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Import-Core26.ps1 -Destination "C:\VMs\Green2-Core26" -Username green2 -Start
```

脚本交互输入密码，不把密码放进命令行。用户名可改为自己的名字，允许小写字母、数字、下划线和连字符，首字符不能是数字；不允许 root。密码为 8–256 个字符，不允许换行。首次启动约需一分钟，从虚拟机控制台查看 DHCP 地址，然后 `ssh 用户名@地址`。

OVA 声明两个可配置属性：`guestinfo.green2.username`（默认 green2）和 `guestinfo.green2.password`（默认空、密码输入框）。支持 OVF 属性的部署器可在部署界面填写。vSphere 部署路径尚未实测；Workstation 使用上面的脚本传递属性，直接双击 OVA 导入不会可靠地传入这两个值。

密码为空时初始化服务等待配置，不创建空密码账号。属性只在首次创建账号时生效，随后用 `passwd` 修改密码，修改部署属性不会重置已有账号。配置完成状态存放在 `/var/lib/cloud/green2-ova-provisioned.json`，不含密码。部署属性可能保存在宿主机 VMX / 管理平台中；首次登录后可关机移除密码属性。该开发镜像的模型为 dangerous，未启用磁盘加密。

Green2 原生 USB 需在 VMware 中手动连接到此虚拟机，未配置自动抢占。NAN 配对 PIN `123456` 与系统登录密码不同。

## 构建与打包

在已准备好预编译运行库、内核模块及固定版本 snaps 的 WORK_DIR 上：

```sh
sudo ./services/ubuntu-core26/build-image.sh --ova WORK_DIR NEW_IMAGE_WORK_DIR
```

OVA 模式要求 `app-tree/usr/bin/vmware-rpctool` 已由 `stage-runtime.py` 放入，且 `app-tree/services/provision_ova.py` 存在。Dockerfile 从发行版预编译 open-vm-tools 包提供 RPC 工具，不编译 VMware 工具源码。

用预编译 qemu-img 把 `image/pc.img` 转为 monolithicSparse VMDK，创建采用 EFI、关闭 Secure Boot 的 VMware VMX。用 VMware OVF Tool 从从未启动的 VMX 导出 OVF：

```sh
ovftool --shaAlgorithm=SHA256 --compress=6 Green2-Core26.vmx Green2-Core26.ovf
python3 services/ubuntu-core26/package-ova.py Green2-Core26.ovf Green2-Core26.ova
```

打包脚本添加账号属性并重建 SHA256 manifest，输出 OVA 及 `.ova.sha256`。必须使用未添加 ProductSection 的干净 OVF。勿导出已经创建个人账号、连接手机配对记录的虚拟机磁盘。
