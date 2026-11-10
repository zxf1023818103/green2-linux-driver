// Realtek SDK NANDOW wire structures; no pointers are transmitted.
#pragma once
#include <cstdint>
#include "rtw_nan_vendor_def.h"
union nandow_para {
	struct rtw_nan_device_capability dev_cap;
	struct rtw_nan_driver_capabilities drv_cap;
	struct rtw_nan_enable nan_enabled;
	struct rtw_nan_cluster_id cluster_id;
	struct rtw_nan_master_preference master_pref;
	struct rtw_nan_random_factor random_factor;
	struct rtw_nan_discovery_window_awake_period dw_awake_period;
	struct rtw_nan_primary_master_channel pri_master_ch;
	struct rtw_nan_secondary_master_channel second_master_ch;
	struct rtw_nan_discovery_beacon_period disc_bcn_period;
	struct rtw_nan_forced_discovery_beacon_transmission forced_disc_bcn;
	struct rtw_nan_publish_data publish_srv;
	struct rtw_nan_cancel_publish_data cancel_publish_srv;
	struct rtw_nan_subscribe_data subscribe_srv;
	struct rtw_nan_cancel_subscribe_data cancel_sub_srv;
	struct rtw_nan_follow_up_transmit_data followup_srv;
	struct rtw_nan_datapath_request datapath_req;
	struct rtw_nan_datapath_end datapath_end;
	struct rtw_nan_datapath_response datapath_rsp;
	struct rtw_nan_datapath_confirm datapath_confirm;
	struct rtw_nan_set_scan_control set_scan_ctl;
	struct rtw_nan_country_code_data country_code;
};

struct nan_customer_nandow {
	uint8_t cmd_id;
	uint8_t cmd_type; // nan_customer_nandow_cmd_type_t
	uint32_t para_len;
	union nandow_para para;
};


static_assert(offsetof(nan_customer_nandow,para)==8);
