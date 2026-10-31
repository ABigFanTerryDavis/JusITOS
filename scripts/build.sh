#!/usr/bin/env bash
# ============================================================================
# JusITOS 0.0.1 — FreeBSD build and image-assembly script
#
# Builds FreeBSD 14.2-RELEASE for arm64/aarch64 and creates a raw GPT disk
# image with an EFI System Partition and a UFS root partition. Image creation
# uses FreeBSD mdconfig/gpart/newfs/mount and therefore requires a FreeBSD
# host with root privileges. Linux may build jusit-init separately (see
# scripts/build_init.sh), but this script does not build the OS image on Linux.
# ============================================================================
set -euo pipefail

# ---- Host checks ----
if [ "$(uname -s)" != "FreeBSD" ]; then
    echo "[jusit] ERROR: scripts/build.sh requires a FreeBSD host; Linux is supported only for the separate init cross-build." >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "[jusit] ERROR: scripts/build.sh needs root privileges for installworld and mdconfig/gpart/mount." >&2
    echo "        Re-run it with sudo or as root." >&2
    exit 1
fi

# ---- Config ----
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${PROJECT_ROOT}/build/freebsd-src"
OBJ_DIR="${PROJECT_ROOT}/build/obj"
ROOTFS_DIR="${PROJECT_ROOT}/build/rootfs"
IMG_DIR="${PROJECT_ROOT}/build"
IMG_FILE="${IMG_DIR}/jusitos-0.0.1.img"
FREEBSD_BRANCH="releng/14.2"
TARGET="arm64"
TARGET_ARCH="aarch64"
KERNCONF="JusITOS-ARM64"
ESP_SIZE_MB=64

# Ensure the replacement init exists before starting the expensive OS build.
BUILD_HOST_ARCH="$(uname -m)"
INIT_BIN="${PROJECT_ROOT}/build/jusit-init"
if [ "$BUILD_HOST_ARCH" = "arm64" ] || [ "$BUILD_HOST_ARCH" = "aarch64" ]; then
    echo "[jusit] native arm64 host; building jusit-init ..."
    "${PROJECT_ROOT}/scripts/build_init.sh"
elif [ ! -s "$INIT_BIN" ]; then
    echo "[jusit] ERROR: ${INIT_BIN} is missing on this cross-build host." >&2
    echo "        Build it first with JUSIT_SYSROOT=/path/to/freebsd-14.2-arm64-sysroot scripts/build_init.sh." >&2
    exit 1
fi

if [ -n "${JOBS:-}" ]; then
    BUILD_JOBS="$JOBS"
elif command -v nproc >/dev/null 2>&1; then
    BUILD_JOBS="$(nproc)"
else
    BUILD_JOBS="$(sysctl -n hw.ncpu)"
fi

case "$BUILD_JOBS" in
    ''|*[!0-9]*)
        echo "[jusit] ERROR: JOBS must be a positive integer." >&2
        exit 2
        ;;
esac
if [ "$BUILD_JOBS" -lt 1 ]; then
    echo "[jusit] ERROR: JOBS must be at least 1." >&2
    exit 2
fi

mkdir -p "$IMG_DIR"
echo "[jusit] project root : $PROJECT_ROOT"
echo "[jusit] FreeBSD src  : $FREEBSD_BRANCH"
echo "[jusit] target       : ${TARGET}/${TARGET_ARCH}"
echo "[jusit] kernel config: $KERNCONF"
echo "[jusit] build jobs   : $BUILD_JOBS"
echo

# ---- Step 1 — clone FreeBSD source if absent ----
if [ ! -d "$SRC_DIR/.git" ]; then
    echo "[1/6] cloning freebsd-src ($FREEBSD_BRANCH) ..."
    git clone --depth 1 -b "$FREEBSD_BRANCH" \
        https://git.freebsd.org/src.git "$SRC_DIR"
else
    echo "[1/6] freebsd-src already present, skipping clone."
fi

# ---- Step 2 — copy JusITOS kernel config into the source tree ----
echo "[2/6] installing JusITOS-ARM64 kernel config ..."
mkdir -p "$SRC_DIR/sys/arm64/conf"
cp "${PROJECT_ROOT}/src/sys/arm64/conf/${KERNCONF}" \
   "$SRC_DIR/sys/arm64/conf/${KERNCONF}"

# ---- Step 3 — build kernel and world ----
echo "[3/6] building kernel ..."
cd "$SRC_DIR"
export MAKEOBJDIRPREFIX="$OBJ_DIR"
export TARGET
export TARGET_ARCH
export KERNCONF
make -j"$BUILD_JOBS" buildkernel

echo "[4/6] building world ..."
make -j"$BUILD_JOBS" buildworld NO_KERBEROS=YES NO_OPENSSH=YES \
                              NO_SENDMAIL=YES NO_NIS=YES \
                              NO_PROFILE=YES NO_BIND=YES

# ---- Step 4 — assemble the staged root filesystem ----
echo "[5/6] assembling rootfs at $ROOTFS_DIR ..."
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR"/{bin,sbin,etc,dev,proc,boot,lib,usr/local/bin,root,mnt,tmp}
make installkernel DESTDIR="$ROOTFS_DIR"
make installworld DESTDIR="$ROOTFS_DIR" NO_KERBEROS=YES NO_OPENSSH=YES \
                                    NO_SENDMAIL=YES NO_NIS=YES \
                                    NO_PROFILE=YES NO_BIND=YES

# Install the replacement init; the host-specific preflight above requires it.
cp "$INIT_BIN" "$ROOTFS_DIR/sbin/init"
chmod 755 "$ROOTFS_DIR/sbin/init"
cp "$INIT_BIN" "$ROOTFS_DIR/bin/jusit-init"

cat > "$ROOTFS_DIR/etc/rc.conf" <<'EOF'
hostname="jusitos"
ifconfig_DEFAULT="DHCP"
sshd_enable="NO"
syslogd_enable="NO"
sendmail_enable="NONE"
EOF

# The QEMU virtio-blk PCI disk is vtbd0; GPT partition 2 is the UFS root.
cat > "$ROOTFS_DIR/etc/fstab" <<'EOF'
/dev/vtbd0p2 / ufs rw 1 1
proc /proc procfs rw 0 0
EOF

# Tell the FreeBSD loader/kernel which GPT UFS partition is the root.
cat > "$ROOTFS_DIR/boot/loader.conf" <<'EOF'
vfs.root.mountfrom="ufs:/dev/vtbd0p2"
EOF
if [ "${JUSIT_LOADER_CONSOLE:-}" = "comconsole" ]; then
    printf 'console="comconsole"\n' >> "$ROOTFS_DIR/boot/loader.conf"
elif [ -n "${JUSIT_LOADER_CONSOLE:-}" ]; then
    echo "[jusit] ERROR: JUSIT_LOADER_CONSOLE supports only 'comconsole' when set." >&2
    exit 2
fi

cat > "$ROOTFS_DIR/etc/ttys" <<'EOF'
console "/sbin/init" vt100 on secure
EOF

if [ ! -s "$ROOTFS_DIR/boot/loader.efi" ]; then
    echo "[jusit] ERROR: $ROOTFS_DIR/boot/loader.efi is missing; cannot create the UEFI boot image." >&2
    exit 1
fi

# Size the raw image from the staged rootfs; leave room for the ESP, UFS
# metadata, and free space so a complete installworld fits beyond 256 MiB.
ROOTFS_SIZE_MB="$(du -sm "$ROOTFS_DIR" | awk '{print $1}')"
MIN_IMG_SIZE_MB=$((ROOTFS_SIZE_MB + ESP_SIZE_MB + 64))
DEFAULT_IMG_SIZE_MB=$((ROOTFS_SIZE_MB + ESP_SIZE_MB + 128))
IMG_SIZE_MB="${IMG_SIZE_MB:-$DEFAULT_IMG_SIZE_MB}"
case "$IMG_SIZE_MB" in
    ''|*[!0-9]*)
        echo "[jusit] ERROR: IMG_SIZE_MB must be a positive integer." >&2
        exit 2
        ;;
esac
if [ "$IMG_SIZE_MB" -lt "$MIN_IMG_SIZE_MB" ]; then
    echo "[jusit] ERROR: IMG_SIZE_MB=${IMG_SIZE_MB} is too small for the staged rootfs (${ROOTFS_SIZE_MB} MiB); use at least ${MIN_IMG_SIZE_MB} MiB." >&2
    exit 2
fi

# ---- Step 5 — pack the UEFI-bootable GPT image ----
echo "[6/6] packing GPT/UEFI image (${IMG_SIZE_MB} MiB) ..."
rm -f "$IMG_FILE"
truncate -s "${IMG_SIZE_MB}M" "$IMG_FILE"
MD_UNIT=""
ROOT_MOUNTPOINT=""
ESP_MOUNTPOINT=""
ROOT_MOUNTED=0
ESP_MOUNTED=0

cleanup_image() {
    set +e
    if [ "$ROOT_MOUNTED" -eq 1 ]; then
        umount "$ROOT_MOUNTPOINT"
    fi
    if [ "$ESP_MOUNTED" -eq 1 ]; then
        umount "$ESP_MOUNTPOINT"
    fi
    if [ -n "$MD_UNIT" ]; then
        MD_NAME="${MD_UNIT##*/}"
        mdconfig -d -u "${MD_NAME#md}"
    fi
    [ -z "$ROOT_MOUNTPOINT" ] || rmdir "$ROOT_MOUNTPOINT"
    [ -z "$ESP_MOUNTPOINT" ] || rmdir "$ESP_MOUNTPOINT"
}
trap cleanup_image EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# gpart must operate on the attached md disk device, not on the image file.
MD_UNIT="$(mdconfig -a -t vnode -f "$IMG_FILE")"
MD_NAME="${MD_UNIT##*/}"
MD_DEVICE="/dev/$MD_NAME"
MD_UNIT_NUMBER="${MD_NAME#md}"
gpart create -s GPT "$MD_DEVICE"
gpart add -t efi -a 1M -s "${ESP_SIZE_MB}M" "$MD_DEVICE"
gpart add -t freebsd-ufs -a 1M "$MD_DEVICE"
ESP_PART="${MD_DEVICE}p1"
ROOT_PART="${MD_DEVICE}p2"

newfs_msdos -F 32 -c 1 -L EFISYS "$ESP_PART"
newfs -U "$ROOT_PART"
ESP_MOUNTPOINT="$(mktemp -d "${TMPDIR:-/tmp}/jusitos-esp.XXXXXX")"
ROOT_MOUNTPOINT="$(mktemp -d "${TMPDIR:-/tmp}/jusitos-root.XXXXXX")"
mount -t msdosfs "$ESP_PART" "$ESP_MOUNTPOINT"
ESP_MOUNTED=1
mount "$ROOT_PART" "$ROOT_MOUNTPOINT"
ROOT_MOUNTED=1

cp -a "$ROOTFS_DIR"/. "$ROOT_MOUNTPOINT"/
mkdir -p "$ESP_MOUNTPOINT/EFI/BOOT" "$ESP_MOUNTPOINT/EFI/FreeBSD"
cp "$ROOTFS_DIR/boot/loader.efi" "$ESP_MOUNTPOINT/EFI/FreeBSD/loader.efi"
# loader.efi reads this EFI-partition setting before it can load loader.conf.
cat > "$ESP_MOUNTPOINT/EFI/FreeBSD/loader.env" <<'EOF'
rootdev=disk0p2
EOF
# UEFI removable-media fallback path for AArch64 systems.
cp "$ROOTFS_DIR/boot/loader.efi" "$ESP_MOUNTPOINT/EFI/BOOT/BOOTAA64.EFI"

# Explicitly unmount before detaching; the EXIT trap remains a safety net.
umount "$ROOT_MOUNTPOINT"
ROOT_MOUNTED=0
umount "$ESP_MOUNTPOINT"
ESP_MOUNTED=0
mdconfig -d -u "$MD_UNIT_NUMBER"
MD_UNIT=""
rmdir "$ROOT_MOUNTPOINT" "$ESP_MOUNTPOINT"
ROOT_MOUNTPOINT=""
ESP_MOUNTPOINT=""
trap - EXIT INT TERM

echo
echo "[jusit] BUILD COMPLETE"
echo "        image: $IMG_FILE"
echo "        size : ${IMG_SIZE_MB} MiB"
echo "        root : GPT freebsd-ufs (vtbd0p2)"
echo "        boot : EFI/BOOT/BOOTAA64.EFI -> FreeBSD loader.efi"
