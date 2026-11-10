#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${HOSTAP_DIR:?Set HOSTAP_DIR to the compatible hostap PASN build}"
exec make -C "$root/tests" HOSTAP_DIR="$HOSTAP_DIR" run "$@"
