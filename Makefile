# Standalone Linux entry point. Kernel module composition lives in Kbuild.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
-include $(ROOT)/.config
export CONFIG_AMEBADPLUS CONFIG_AMEBAGREEN2 CONFIG_AMEBALITE CONFIG_AMEBAX
export CONFIG_WHC_HCI_USB CONFIG_WHC_HCI_SDIO CONFIG_WHC_HCI_SPI CONFIG_WHC_HCI_GSPI
export CONFIG_NAN CONFIG_P2P CONFIG_MCC CONFIG_BT_INIC CONFIG_IEEE80211R CONFIG_SUPPLICANT_SME
export CONFIG_WHC_CMD_PATH CONFIG_WHC_WIFI_API_PATH CONFIG_WHC_HOST_RTW_CLI
export CONFIG_RMESH CONFIG_FW_DOWNLOAD CONFIG_WHCH CONFIG_WHC_USB_BULK_2048
KDIR ?= /lib/modules/$(shell uname -r)/build
.PHONY: all modules clean prepare list-defconfigs menuconfig
all: modules
modules:
	$(MAKE) -C $(KDIR) M=$(ROOT) modules
clean:
	$(MAKE) -C $(KDIR) M=$(ROOT) clean
prepare:
	$(ROOT)/whc_setup.sh
list-defconfigs:
	@ls $(ROOT)/configs
%_defconfig:
	@test -f $(ROOT)/configs/$@
	cp $(ROOT)/configs/$@ $(ROOT)/.config
menuconfig:
	KCONFIG_CONFIG=$(ROOT)/.config python3 -m menuconfig $(ROOT)/Kconfig
