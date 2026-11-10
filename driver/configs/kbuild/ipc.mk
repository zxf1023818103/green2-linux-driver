ccflags-$(CONFIG_NAN) += -DCONFIG_NAN
ccflags-$(CONFIG_P2P) += -DCONFIG_P2P
ccflags-$(CONFIG_IEEE80211R) += -DCONFIG_IEEE80211R
ccflags-$(CONFIG_SUPPLICANT_SME) += -DCONFIG_SUPPLICANT_SME
ccflags-$(CONFIG_WHC_WIFI_API_PATH) += -DCONFIG_WHC_WIFI_API_PATH

ccflags-y += -DCONFIG_WHC_HOST_LINUX
ccflags-y += -I$(src) -I$(src)/../shared/include
ccflags-y += -DCONFIG_WHC_HCI_IPC
ccflags-y += -I$(srctree)/../../firmware/component/wifi/driver/include
ccflags-y += -I$(srctree)/../../firmware/component/wifi/driver/intf
ccflags-y += -I$(srctree)/../../firmware/component/wifi/api
ccflags-y += -I$(srctree)/../../firmware/component/wifi/whc
ccflags-y += -I$(srctree)/../../firmware/component/wifi/whc/whc_common
ccflags-y += -I$(srctree)/../../firmware/component/wifi/whc/whc_common/ipc
ccflags-y += -I$(srctree)/../../firmware/component/wifi/whc/whc_dev

obj-$(CONFIG_WIFI_RTK_CFG80211_FULLMAC) += whc-ipc.o
whc-ipc-objs := whc_host_wiphy.o \
		whc_host_regd.o \
		whc_host_ethtool_ops.o \
		whc_host_netdev_ops.o \
		whc_host_netdev_ops_p2p.o \
		whc_host_cfgvendor.o \
		whc_host_proc.o \
		whc_host_ops_sta.o \
		whc_host_event_hdl.o \
		whc_host_ops_ap.o \
		whc_host_ops_key.o \
		whc_host_ops_nan.o \
		whc_host_ops_p2p.o \
		whc_host_acs.o \
		whc_host_promisc.o \
		whc_host_ft.o \
		whc_ipc_host_probe.o \
		whc_host_ops.o \
		whc_ipc_host_hci.o \
		whc_ipc_host_event_tx.o \
		whc_ipc_host_event_rx.o \
		whc_ipc_host_recv.o \
		whc_ipc_host_xmit.o \
		whc_ipc_host_msg.o \
		whc_ipc_host_mem.o \
		ameba_wificfg.o

ifeq ($(CONFIG_WHC_CMD_PATH),y)
ccflags-y += -DCONFIG_WHC_CMD_PATH
whc-ipc-objs += whc_host_netlink.o
endif
