# Build against the prepared, matching Ubuntu kernel headers.
KDIR ?= /lib/modules/$(shell uname -r)/build
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(ROOT) modules
clean:
	$(MAKE) -C $(KDIR) M=$(ROOT) clean
