# Shared kernel-module composition for Linux and Android.
# External Android builds without module Kconfig default to the validated USB profile.
ifeq ($(strip $(CONFIG_WHC_HCI_USB)$(CONFIG_WHC_HCI_SDIO)$(CONFIG_WHC_HCI_SPI)$(CONFIG_WHC_HCI_GSPI)),)
include $(src)/configs/amebagreen2_usb_nan_bulk2048_defconfig
endif
ifeq ($(CONFIG_WHC_HCI_USB),y)
include $(src)/src/usb/Kbuild
else ifeq ($(CONFIG_WHC_HCI_SDIO),y)
include $(src)/src/sdio/Kbuild
else ifeq ($(CONFIG_WHC_HCI_SPI),y)
include $(src)/src/spi/Kbuild
else ifeq ($(CONFIG_WHC_HCI_GSPI),y)
include $(src)/src/gspi/Kbuild
else
$(error Select an external WHC bus configuration; SDK in-tree IPC is not integrated here)
endif
