HOSTAP_DIR ?= ../app/wifi_nan_utility/hostap-pasn
PKG_CONFIG ?= pkg-config
CFLAGS += -std=gnu11 -O2 -Wall -ffunction-sections -fdata-sections
CFLAGS += -DCONFIG_SAE -DCONFIG_PASN -DCONFIG_NAN -D_GNU_SOURCE
CFLAGS += -I$(HOSTAP_DIR)/src -I$(HOSTAP_DIR)/src/utils
CFLAGS += $(shell $(PKG_CONFIG) --cflags libnl-3.0 libnl-genl-3.0)
LDFLAGS += -L$(HOSTAP_DIR)/wpa_supplicant -Wl,--gc-sections
LDLIBS += -lpasn -lcrypto $(shell $(PKG_CONFIG) --libs libnl-3.0 libnl-genl-3.0)
TESTS := test_nan_pasn_core test_nan_pasn_setup test_nan_pasn_retransmit
BINS := $(addprefix out/,$(TESTS))
.PHONY: all run clean
all: $(BINS)
out/%: %.c ../app/wifi_nan_utility/pasn_probe/core/green2_pasn.c ../app/wifi_nan_utility/pasn_probe/core/green2_pasn.h
	mkdir -p out
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)
run: all
	@set -e; for test in $(BINS); do LD_LIBRARY_PATH=$(HOSTAP_DIR)/wpa_supplicant:$$LD_LIBRARY_PATH ./$$test; done
clean:
	rm -rf out
