ccflags-$(CONFIG_AMEBADPLUS)           += -DCONFIG_AMEBADPLUS
ccflags-$(CONFIG_AMEBAGREEN2)          += -DCONFIG_AMEBAGREEN2
ccflags-$(CONFIG_AMEBALITE)            += -DCONFIG_AMEBALITE
ccflags-$(CONFIG_AMEBAX)               += -DCONFIG_AMEBAX

ccflags-$(CONFIG_NAN) += -DCONFIG_NAN
ccflags-$(CONFIG_P2P) += -DCONFIG_P2P
ccflags-$(CONFIG_MCC) += -DCONFIG_MCC
ccflags-$(CONFIG_IEEE80211R) += -DCONFIG_IEEE80211R
ccflags-$(CONFIG_BT_INIC) += -DCONFIG_BT_INIC
ccflags-$(CONFIG_WHC_HCI_GSPI) += -DCONFIG_WHC_HCI_GSPI
ccflags-$(CONFIG_WHC_CMD_PATH) += -DCONFIG_WHC_CMD_PATH
ccflags-$(CONFIG_WHC_WIFI_API_PATH) += -DCONFIG_WHC_WIFI_API_PATH
CONFIG_WHC_HOST_LOG_FWD := y
ccflags-$(CONFIG_WHC_HOST_LOG_FWD) += -DCONFIG_WHC_HOST_LOG_FWD
ccflags-$(CONFIG_SUPPLICANT_SME) += -DCONFIG_SUPPLICANT_SME
ccflags-$(CONFIG_RMESH) += -DCONFIG_RMESH
ccflags-$(CONFIG_FW_DOWNLOAD) += -DCONFIG_FW_DOWNLOAD
ccflags-$(CONFIG_WHCH) += -DCONFIG_WHCH

ccflags-y += -DCONFIG_WHC_HOST_LINUX
ccflags-y += -I$(src) -I$(src)/../shared/include


#for ko size, no debug info if enabled
#EXTRA_CFLAGS += -g0

whc_gspi-$(CONFIG_WHC_HCI_GSPI) := \
		whc_gspi_host_drvio.o \
		whc_gspi_host_ops.o \
		whc_gspi_host_probe.o \
		whc_gspi_host_init.o	\
		whc_host_ethtool_ops.o \
		whc_host_netdev_ops.o \
		whc_host_trx.o \
		whc_host_pkt_tx.o \
		whc_host_pkt_rx.o \
		whc_host_memory.o \
		whc_host_hci.o \
		whc_host_ft.o \
		whc_host_ops_nan.o \
		whc_host_ops_p2p.o \
		whc_host_netdev_ops_p2p.o \
		whc_host_cfgvendor.o

ifeq ($(CONFIG_WHCH), y)
    whc_gspi-$(CONFIG_WHC_HCI_GSPI) += \
		whch_host_pkt_tx_proc.o \
		whch_host_pkt_rx_proc.o \
		whch_host_sta_mgt.o \
		whch_host_security.o \
		whch_host_arc4.o \
		whch_host_mlme.o \
		whch_host_addba.o \
		whch_host_hal_xmit_com.o \
		whch_host_hal_recv_com.o
ifeq ($(CONFIG_NAN), y)
    whc_gspi-$(CONFIG_WHC_HCI_GSPI) += \
		whch_host_nan.o
endif
ifeq ($(CONFIG_AMEBAGREEN2), y)
    whc_gspi-$(CONFIG_WHC_HCI_GSPI) += \
		whch_host_hal_xmit.o \
		whch_host_hal_rate.o \
		whch_host_hal_recv.o
endif
endif

ifeq ($(CONFIG_WHC_WIFI_API_PATH), y)
    whc_gspi-$(CONFIG_WHC_HCI_GSPI) += \
			whc_host_proc.o \
			whc_host_ops_sta.o \
			whc_host_ops_ap.o \
			whc_host_ops.o \
			whc_host_event_tx.o \
			whc_host_event_rx.o \
			whc_host_event_hdl.o \
			whc_host_wiphy.o \
			whc_host_regd.o \
			whc_host_protocal_offload.o \
			whc_host_ops_key.o \
			whc_host_acs.o

endif

ifeq ($(CONFIG_WHC_CMD_PATH), y)
    whc_gspi-$(CONFIG_WHC_HCI_GSPI) += \
            whc_host_netlink.o \
            whc_host_cmd_path_tx.o
    whc_gspi-$(CONFIG_WHC_HOST_LOG_FWD) += whc_host_log_fwd.o

    ifeq ($(CONFIG_WHC_HOST_RTW_CLI),y)
        ccflags-$(CONFIG_WHC_HOST_RTW_CLI) += -DCONFIG_WHC_HOST_RTW_CLI
        ccflags-y += -I$(src)

        whc_gspi-$(CONFIG_WHC_HCI_GSPI) += \
               whc_host_rtw_cli_custom_netlink.o \
               whc_host_rtw_cli_api.o
    endif

endif

whc_gspi-$(CONFIG_BT_INIC) += rtb_gspi.o

include $(src)/configs/kbuild/fwdl.mk
whc_gspi-$(CONFIG_FW_DOWNLOAD) += $(FWDL_OBJS)

obj-m += whc_gspi.o
