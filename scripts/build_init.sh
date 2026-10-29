#!/usr/bin/env bash
# ============================================================================
# JusITOS 0.0.1 — build the AArch64 init binary
#
# Native build: FreeBSD/arm64 with cc.
# Cross-build: Clang on Linux or another host with JUSIT_SYSROOT set to a
# FreeBSD 14.2 arm64 sysroot. This builds only the init executable; the full
# OS/image build in scripts/build.sh requires a FreeBSD host.
# ============================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${PROJECT_ROOT}/src/init/jusit_init.c"
OUT="${PROJECT_ROOT}/build/jusit-init"
mkdir -p "$(dirname "$OUT")"

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"
if [ "$HOST_OS" = "FreeBSD" ] && \
   { [ "$HOST_ARCH" = "arm64" ] || [ "$HOST_ARCH" = "aarch64" ]; } && \
   command -v cc >/dev/null 2>&1; then
    echo "[jusit-init] native FreeBSD arm64 build"
    cc -O2 -static -o "$OUT" "$SRC"
elif command -v clang >/dev/null 2>&1; then
    if [ -z "${JUSIT_SYSROOT:-}" ]; then
        echo "[jusit-init] ERROR: cross-build requires JUSIT_SYSROOT pointing to a FreeBSD 14.2 arm64 sysroot." >&2
        echo "             This builds only jusit-init; use a FreeBSD host for scripts/build.sh." >&2
        exit 2
    fi
    if [ ! -d "$JUSIT_SYSROOT" ]; then
        echo "[jusit-init] ERROR: JUSIT_SYSROOT is not a directory: $JUSIT_SYSROOT" >&2
        exit 2
    fi
    echo "[jusit-init] cross-compiling with clang (target: aarch64-unknown-freebsd14.2)"
    clang --target=aarch64-unknown-freebsd14.2 \
          -O2 -static \
          --sysroot="$JUSIT_SYSROOT" \
          -o "$OUT" "$SRC"
else
    echo "[jusit-init] ERROR: no suitable compiler found; install Clang or build natively on FreeBSD arm64." >&2
    exit 1
fi

echo "[jusit-init] binary written to $OUT ($(wc -c < "$OUT") bytes)"
