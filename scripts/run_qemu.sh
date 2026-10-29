#!/usr/bin/env bash
# ==========================================================================
#  JusITOS 0.0.1 — QEMU launcher
# ==========================================================================
#  Boots the JusITOS arm64 image in QEMU's 'virt' machine with:
#    * 2 GB RAM, 2 CPUs
#    * virtio-blk as the boot disk
#    * virtio-gpu (so we get a real framebuffer /dev/fb0)
#    * virtio-keyboard for input
#    * UART on stdout for early-boot messages
#
#  Usage:
#    ./run_qemu.sh                # boot with display window
#    ./run_qemu.sh --headless     # serial-only (for batch testing)
#    ./run_qemu.sh --debug        # add -d in_asm,cpu_reset + gdbstub :1234
# ==========================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${PROJECT_ROOT}/build/jusitos-0.0.1.img"
OVMF_DIR="${OVMF_DIR:-/usr/share/edk2/qemu}"   # set env to override

if [ ! -f "$IMG" ]; then
    echo "[jusit-qemu] image not found: $IMG"
    echo "               run scripts/build.sh first."
    exit 1
fi

HEADLESS=0
DEBUG=0
for arg in "$@"; do
    case "$arg" in
        --headless) HEADLESS=1 ;;
        --debug)    DEBUG=1 ;;
        *) echo "unknown arg: $arg"; exit 2 ;;
    esac
done

DISPLAY_ARGS=(-display gtk -vga virtio -device virtio-gpu-pci)
SERIAL_ARGS=(-serial mon:stdio)
if [ "$HEADLESS" = "1" ]; then
    DISPLAY_ARGS=(-display none)
fi

GDB_ARGS=()
if [ "$DEBUG" = "1" ]; then
    GDB_ARGS=(-d in_asm,cpu_reset -gdb tcp::1234 -S)
fi

# BIOS / firmware
FW_ARGS=()
if [ -f "${OVMF_DIR}/QEMU_EFI.fd" ]; then
    FW_ARGS=(-bios "${OVMF_DIR}/QEMU_EFI.fd")
elif [ -f "${OVMF_DIR}/RELEASEAARCH64_QEMU_EFI.fd" ]; then
    FW_ARGS=(-bios "${OVMF_DIR}/RELEASEAARCH64_QEMU_EFI.fd")
fi

exec qemu-system-aarch64 \
    -machine virt,gic-version=3 \
    -cpu cortex-a57 -smp 2 -m 2048 \
    "${FW_ARGS[@]}" \
    "${DISPLAY_ARGS[@]}" \
    "${SERIAL_ARGS[@]}" \
    "${GDB_ARGS[@]}" \
    -drive if=none,file="$IMG",format=raw,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -device virtio-keyboard-pci \
    -device virtio-mouse-pci \
    -device virtio-net-device,netdev=net0 \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-rng-pci \
    -rtc base=utc \
    -no-reboot
