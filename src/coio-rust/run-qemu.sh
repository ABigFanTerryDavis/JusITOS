#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/coio-rust"
ESP_ROOT="${BUILD_DIR}/esp"
EFI_BINARY="${ESP_ROOT}/EFI/BOOT/BOOTAA64.EFI"
QEMU="${QEMU:-qemu-system-aarch64}"
UEFI_CODE="${UEFI_CODE:-/usr/share/AAVMF/AAVMF_CODE.fd}"
UEFI_VARS="${UEFI_VARS:-/usr/share/AAVMF/AAVMF_VARS.fd}"
VARS_FILE="${COIO_VARS_FILE:-${BUILD_DIR}/AAVMF_VARS.fd}"

if [ ! -s "$EFI_BINARY" ]; then
    echo "[coio-rust] ERROR: staged EFI app not found: $EFI_BINARY" >&2
    echo "               run src/coio-rust/build.sh first." >&2
    exit 1
fi
if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "[coio-rust] ERROR: QEMU executable not found: $QEMU" >&2
    exit 1
fi
if [ ! -f "$UEFI_CODE" ] || [ ! -f "$UEFI_VARS" ]; then
    echo "[coio-rust] ERROR: AAVMF code/vars files must both exist." >&2
    echo "               UEFI_CODE=$UEFI_CODE" >&2
    echo "               UEFI_VARS=$UEFI_VARS" >&2
    exit 1
fi

DISPLAY_ARGS=(-device ramfb)
case "${COIO_HEADLESS:-0}" in
    1|true|TRUE|yes|YES)
        DISPLAY_ARGS+=(-display none)
        ;;
    0|false|FALSE|no|NO)
        DISPLAY_ARGS+=(-display gtk)
        ;;
    *)
        echo "[coio-rust] ERROR: COIO_HEADLESS must be 0/1, false/true, or no/yes." >&2
        exit 2
        ;;
esac

mkdir -p "$BUILD_DIR"
if [ ! -f "$VARS_FILE" ]; then
    cp "$UEFI_VARS" "$VARS_FILE"
fi

# QEMU's FAT directory backend exposes this isolated ESP tree as a FAT volume;
# no disk image or existing jkernel/QEMU path is read or modified.
"$QEMU" \
    -machine virt,gic-version=3 \
    -cpu cortex-a57 -smp 1 -m 512 \
    -drive "if=pflash,format=raw,unit=0,file=${UEFI_CODE},readonly=on" \
    -drive "if=pflash,format=raw,unit=1,file=${VARS_FILE}" \
    -drive "if=none,format=raw,file=fat:rw:${ESP_ROOT},id=coioesp" \
    -device virtio-blk-pci,drive=coioesp,bootindex=0 \
    "${DISPLAY_ARGS[@]}" -serial stdio -monitor none \
    -no-reboot \
    "$@"
