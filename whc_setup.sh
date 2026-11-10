#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
for header in ameba_inic.h ameba_wificfg_common.h bt_inic_defs.h rtw_inic_common.h rtw_wifi_common.h whc_def.h wifi_api_event.h wifi_api_types.h wifi_intf_drv_to_app_internal.h; do
    test -s "$root/include/sdk/$header" || { echo "Missing shipped SDK header: $header" >&2; exit 1; }
done
echo "Standalone SDK headers ready (provenance: UPSTREAM.json)."
