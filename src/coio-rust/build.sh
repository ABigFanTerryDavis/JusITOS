#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUST_PROJECT="${PROJECT_ROOT}/src/coio-rust"
BUILD_DIR="${PROJECT_ROOT}/build/coio-rust"
TARGET="aarch64-unknown-uefi"
CARGO="${CARGO:-cargo}"

if ! command -v "$CARGO" >/dev/null 2>&1; then
    echo "[coio-rust] ERROR: cargo was not found: $CARGO" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"
"$CARGO" build \
    --manifest-path "${RUST_PROJECT}/Cargo.toml" \
    --target "$TARGET" \
    --target-dir "${BUILD_DIR}/target" \
    --release

EFI_BINARY="${BUILD_DIR}/target/${TARGET}/release/coio-rust.efi"
if [ ! -s "$EFI_BINARY" ]; then
    echo "[coio-rust] ERROR: expected UEFI binary was not produced: $EFI_BINARY" >&2
    exit 1
fi

ESP_ROOT="${BUILD_DIR}/esp"
mkdir -p "${ESP_ROOT}/EFI/BOOT"
install -m 0644 "$EFI_BINARY" "${ESP_ROOT}/EFI/BOOT/BOOTAA64.EFI"
echo "[coio-rust] staged ${ESP_ROOT}/EFI/BOOT/BOOTAA64.EFI"
