#!/usr/bin/env bash
# ==========================================================================
#  JusITOS 0.0.1 — Build the splash/init binary (cross-compile for arm64)
# ==========================================================================
#  This is a thin wrapper that invokes cc with the right target triple.
#  Run from a FreeBSD arm64 host OR with a clang cross-compiler on Linux.
#
#  On FreeBSD arm64:
#      cc -O2 -static -o build/jusit-init src/init/jusit_init.c
#
#  On Linux with clang cross-compiler installed:
#      clang --target=aarch64-unknown-freebsd14.2 \
#          -O2 -static -nostdlib \
#          --sysroot=/path/to/freebsd-arm64-sysroot \
#          -o build/jusit-init src/init/jusit_init.c
# ==========================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${PROJECT_ROOT}/src/init/jusit_init.c"
OUT="${PROJECT_ROOT}/build/jusit-init"
mkdir -p "$(dirname "$OUT")"

# Detect environment
if [ -x /usr/bin/cc ] && [ "$(uname -s)" = "FreeBSD" ] && \
   [ "$(uname -m)" = "arm64" ]; then
    echo "[jusit-init] native FreeBSD arm64 build"
    cc -O2 -static -o "$OUT" "$SRC"
elif command -v clang >/dev/null 2>&1; then
    echo "[jusit-init] cross-compiling with clang (target: aarch64-unknown-freebsd14.2)"
    if [ -z "${JUSIT_SYSROOT:-}" ]; then
        echo "[jusit-init] ERROR: set JUSIT_SYSROOT=/path/to/freebsd-arm64-sysroot"
        exit 2
    fi
    clang --target=aarch64-unknown-freebsd14.2 \
          -O2 -static \
          --sysroot="$JUSIT_SYSROOT" \
          -o "$OUT" "$SRC"
else
    echo "[jusit-init] ERROR: no suitable compiler found."
    echo "             Install clang or build on FreeBSD arm64."
    exit 1
fi

echo "[jusit-init] binary written to $OUT ($(wc -c < "$OUT") bytes)"
