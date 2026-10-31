#!/usr/bin/env bash
# ============================================================================
# JusITOS 0.0.1 — QEMU launcher
#
# Boots the GPT/UEFI image on QEMU's arm64 virt machine. The kernel config
# includes PCI virtio for storage/networking and xHCI/USB keyboard drivers.
# QEMU's ramfb device supplies the UEFI GOP framebuffer.
# ============================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${PROJECT_ROOT}/build/jusitos-0.0.1.img"
QEMU="${QEMU:-qemu-system-aarch64}"

if [ ! -f "$IMG" ]; then
    echo "[jusit-qemu] image not found: $IMG" >&2
    echo "               run scripts/build.sh first." >&2
    exit 1
fi
if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "[jusit-qemu] ERROR: QEMU executable not found: $QEMU" >&2
    exit 1
fi

HEADLESS=0
DEBUG=0
for arg in "$@"; do
    case "$arg" in
        --headless) HEADLESS=1 ;;
        --debug)    DEBUG=1 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

# Accept either a monolithic firmware image (UEFI_FIRMWARE) or the standard
# read-only code + writable variables-template pair (UEFI_CODE and UEFI_VARS).
UEFI_FIRMWARE="${UEFI_FIRMWARE:-}"
UEFI_CODE="${UEFI_CODE:-}"
UEFI_VARS="${UEFI_VARS:-}"
FW_ARGS=()
UEFI_RUNTIME_DIR=""

cleanup_uefi() {
    if [ -n "$UEFI_RUNTIME_DIR" ]; then
        rm -rf "$UEFI_RUNTIME_DIR"
    fi
}
trap cleanup_uefi EXIT

if [ -n "$UEFI_FIRMWARE" ]; then
    if [ ! -f "$UEFI_FIRMWARE" ]; then
        echo "[jusit-qemu] ERROR: UEFI_FIRMWARE is not a file: $UEFI_FIRMWARE" >&2
        exit 1
    fi
    FW_ARGS=(-bios "$UEFI_FIRMWARE")
elif [ -n "$UEFI_CODE" ] || [ -n "$UEFI_VARS" ]; then
    if [ -z "$UEFI_CODE" ] || [ -z "$UEFI_VARS" ]; then
        echo "[jusit-qemu] ERROR: set both UEFI_CODE and UEFI_VARS for split pflash firmware." >&2
        exit 1
    fi
    if [ ! -f "$UEFI_CODE" ] || [ ! -f "$UEFI_VARS" ]; then
        echo "[jusit-qemu] ERROR: UEFI_CODE and UEFI_VARS must both name existing files." >&2
        exit 1
    fi
else
    # Monolithic firmware image locations used by common QEMU packages.
    for candidate in \
        /usr/share/qemu-efi-aarch64/QEMU_EFI.fd \
        /usr/share/edk2/aarch64/QEMU_EFI.fd \
        /usr/share/edk2/qemu/QEMU_EFI.fd \
        /usr/local/share/edk2-aarch64/QEMU_EFI.fd \
        /usr/local/share/edk2/aarch64/QEMU_EFI.fd \
        /usr/local/share/qemu-efi-aarch64/QEMU_EFI.fd \
        /usr/local/share/uefi-firmware/QEMU_EFI.fd; do
        if [ -f "$candidate" ]; then
            UEFI_FIRMWARE="$candidate"
            break
        fi
    done

    if [ -n "$UEFI_FIRMWARE" ]; then
        FW_ARGS=(-bios "$UEFI_FIRMWARE")
    else
        # Split pflash locations used by AAVMF and EDK2 packages.
        for candidate_code in \
            /usr/share/AAVMF/AAVMF_CODE.fd \
            /usr/share/edk2/aarch64/QEMU_EFI-pflash.raw \
            /usr/local/share/AAVMF/AAVMF_CODE.fd \
            /usr/local/share/edk2/aarch64/QEMU_EFI-pflash.raw; do
            case "$candidate_code" in
                */AAVMF_CODE.fd)
                    candidate_vars="${candidate_code%/*}/AAVMF_VARS.fd"
                    ;;
                *)
                    candidate_vars="${candidate_code%/*}/vars-template-pflash.raw"
                    ;;
            esac
            if [ -f "$candidate_code" ] && [ -f "$candidate_vars" ]; then
                UEFI_CODE="$candidate_code"
                UEFI_VARS="$candidate_vars"
                break
            fi
        done
    fi
fi

if [ -n "$UEFI_CODE" ] && [ -n "$UEFI_VARS" ]; then
    UEFI_RUNTIME_DIR="$(mktemp -d "${TMPDIR:-/tmp}/jusitos-uefi.XXXXXX")"
    UEFI_VARSTORE="${UEFI_RUNTIME_DIR}/vars.fd"
    cp "$UEFI_VARS" "$UEFI_VARSTORE"
    FW_ARGS=(
        -drive "if=pflash,format=raw,unit=0,file=${UEFI_CODE},readonly=on"
        -drive "if=pflash,format=raw,unit=1,file=${UEFI_VARSTORE}"
    )
    echo "[jusit-qemu] using split UEFI pflash code/vars firmware."
elif [ -n "$UEFI_FIRMWARE" ]; then
    echo "[jusit-qemu] using UEFI firmware: $UEFI_FIRMWARE"
else
    echo "[jusit-qemu] ERROR: no AArch64 UEFI firmware found." >&2
    echo "               Install QEMU AArch64 firmware or set UEFI_FIRMWARE, or set both UEFI_CODE and UEFI_VARS." >&2
    exit 1
fi

DISPLAY_ARGS=(-display gtk)
FRAMEBUFFER_ARGS=(-device ramfb)
SERIAL_ARGS=(-serial mon:stdio)
if [ "$HEADLESS" -eq 1 ]; then
    # Disable host display output, but retain ramfb for the guest /dev/fb0.
    DISPLAY_ARGS=(-display none)
fi

GDB_ARGS=()
if [ "$DEBUG" -eq 1 ]; then
    GDB_ARGS=(-d in_asm,cpu_reset -gdb tcp:127.0.0.1:1234 -S)
fi

"$QEMU" \
    -machine virt,gic-version=3 \
    -cpu cortex-a57 -smp 2 -m 2048 \
    "${FW_ARGS[@]}" \
    "${DISPLAY_ARGS[@]}" \
    "${FRAMEBUFFER_ARGS[@]}" \
    "${SERIAL_ARGS[@]}" \
    "${GDB_ARGS[@]}" \
    -drive if=none,file="$IMG",format=raw,id=hd0 \
    -device virtio-blk-pci,drive=hd0 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    -device virtio-net-pci,netdev=net0 \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 \
    -device virtio-rng-pci \
    -rtc base=utc \
    -no-reboot
