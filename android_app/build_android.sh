#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
: "${ANDROID_HOME:?Set ANDROID_HOME to precompiled SDK 36 with NDK 26.2.11394342}"
: "${GREEN2_SRT_AAR:?Set GREEN2_SRT_AAR to the prebuilt srtdroid-core 1.9.5 AAR}"
: "${GREEN2_SRT_INCLUDE:?Set GREEN2_SRT_INCLUDE to the matching headers}"
exec "${GRADLE_BIN:-gradle}" -p "$root" --no-daemon :app:assembleRelease "$@"
