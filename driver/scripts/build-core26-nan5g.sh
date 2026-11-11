#!/usr/bin/env bash
# Run inside the existing Core26 builder with precompiled matching headers.
set -euo pipefail
[[ $# == 1 && ! -e $1 ]] || { echo "usage: $0 NEW_WORK_DIR" >&2; exit 2; }
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(realpath -m "$1")
kver=7.0.0-38-generic
[[ $(dpkg-query -W -f='${Version}' linux-source-7.0.0) == 7.0.0-38.38 ]] || {
    echo 'Requires linux-source-7.0.0=7.0.0-38.38' >&2; exit 1;
}
test -f "/usr/src/linux-headers-$kver/Module.symvers"
mkdir -p "$work/src"
tar -xjf /usr/src/linux-source-7.0.0.tar.bz2 -C "$work/src" --strip-components=1 \
    linux-source-7.0.0/net/wireless
patch -d "$work/src" -p1 --batch < "$repo/driver/configs/experiments/cfg80211-nan-5ghz-only.patch"
make -C "/usr/src/linux-headers-$kver" M="$work/src/net/wireless" \
    CC="${CC:-gcc-15}" CONFIG_CFG80211=m CONFIG_CFG80211_EXTRA_REGDB_KEYDIR= \
    -j"${JOBS:-4}" modules
cp "$work/src/net/wireless/cfg80211.ko" "$work/cfg80211-nan5g.ko"
strip --strip-debug "$work/cfg80211-nan5g.ko"
modinfo "$work/cfg80211-nan5g.ko" | grep -E 'vermagic|srcversion|nan_allow_5g_only'
(cd "$work" && sha256sum cfg80211-nan5g.ko > cfg80211-nan5g.ko.sha256)
