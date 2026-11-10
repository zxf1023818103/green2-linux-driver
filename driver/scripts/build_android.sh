#!/usr/bin/env bash
# Compile only the external module against a prepared Android kernel.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${KERNEL_OUT:?Set KERNEL_OUT to the prepared Android kernel build directory}"
: "${ANDROID_CLANG_BIN:?Set ANDROID_CLANG_BIN to a prebuilt LLVM bin directory}"
for file in include/config/kernel.release include/generated/autoconf.h; do
    test -f "$KERNEL_OUT/$file" || { echo "Kernel output is not prepared: $file" >&2; exit 1; }
done
test -x "$ANDROID_CLANG_BIN/clang" || { echo "Missing prebuilt clang" >&2; exit 1; }
export PATH="$ANDROID_CLANG_BIN:$PATH"
export KDIR="${KERNEL_SRC:-$KERNEL_OUT}"
exec "$root/scripts/build_linux.sh" O="$KERNEL_OUT" ARCH=arm64 LLVM=1 "$@"
