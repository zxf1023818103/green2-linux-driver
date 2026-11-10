#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
hostap=$(realpath "$1"); output=$(realpath -m "$2"); mkdir -p "$output"
cmake -S "$root/cli_tool" -B /work/services-cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-15 -DGREEN2_HOSTAP_DIR="$hostap" \
    -DGREEN2_BUILD_LEGACY_NAN_TOOLS=ON -DBUILD_TESTING=ON
cmake --build /work/services-cmake --parallel 4
LD_LIBRARY_PATH="$hostap/wpa_supplicant" ctest --test-dir /work/services-cmake --output-on-failure
for name in green2_cli pasn_live pasn_vendor_events rtw_aware libnanapi.so libgreen2pasn.so; do
    install -m755 "/work/services-cmake/$name" "$output/$name"
done
cp "$hostap/wpa_supplicant/libpasn.so" "$output/"
cp "$root/cli_tool/integration/ubuntu-core26/services/"* "$output/"
