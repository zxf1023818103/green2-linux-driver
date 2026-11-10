#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to a prebuilt NDK}"
: "${GREEN2_HOSTAP_DIR:?Set GREEN2_HOSTAP_DIR}"
: "${GREEN2_PASN_LIBRARY:?Set GREEN2_PASN_LIBRARY to Android libpasn.so}"
: "${GREEN2_LIBNL_LIBRARY:?Set GREEN2_LIBNL_LIBRARY to Android libnl.so}"
: "${GREEN2_LIBNL_INCLUDE_DIR:?Set GREEN2_LIBNL_INCLUDE_DIR}"
: "${GREEN2_CRYPTO_LIBRARY:?Set GREEN2_CRYPTO_LIBRARY to the Android crypto provider}"
build=${GREEN2_BUILD_DIR:-$root/build-android}
extra=()
if [[ -n ${GREEN2_ANDROID_ADAPTER_SOURCE:-} ]]; then extra+=("-DGREEN2_ANDROID_ADAPTER_SOURCE=$GREEN2_ANDROID_ADAPTER_SOURCE"); fi
cmake -S "$root" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 -DBUILD_TESTING=ON \
    -DGREEN2_HOSTAP_DIR="$GREEN2_HOSTAP_DIR" -DGREEN2_PASN_LIBRARY="$GREEN2_PASN_LIBRARY" \
    -DGREEN2_LIBNL_LIBRARY="$GREEN2_LIBNL_LIBRARY" -DGREEN2_LIBNL_INCLUDE_DIR="$GREEN2_LIBNL_INCLUDE_DIR" \
    -DGREEN2_CRYPTO_LIBRARY="$GREEN2_CRYPTO_LIBRARY" \
    "${extra[@]}"
cmake --build "$build" --parallel "${BUILD_JOBS:-4}"
