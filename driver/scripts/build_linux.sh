#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kernel_build=${KDIR:-/lib/modules/$(uname -r)/build}
test -f "$kernel_build/Makefile" || { echo "KDIR must name a prepared target kernel build directory" >&2; exit 1; }
if [[ ! -f "$root/.config" ]]; then make -C "$root" amebagreen2_usb_nan_bulk2048_defconfig; fi
make -C "$root" prepare
exec make -C "$root" KDIR="$kernel_build" "$@"
