#!/usr/bin/env bash
# ==========================================================================
#  JusITOS 0.0.1 — Build script
# ==========================================================================
#  Clones FreeBSD 14.2-RELEASE source, builds the JusITOS-ARM64 kernel,
#  compiles the userland splash/init binary, packs a minimal rootfs, and
#  emits a bootable raw disk image ready to feed to QEMU.
#
#  REQUIREMENTS:
#    * Run on FreeBSD 13.x+ or 14.x (preferred), or a Linux host with a
#      working freebsd-cross toolchain. (We strongly recommend building
#      inside a FreeBSD VM if you don't have one natively.)
#    * Enough free disk: ~10 GB for src + objtree, ~200 MB for the image.
#    * Network access for the initial git clone of freebsd-src.
#
#  OUTPUTS:
#    build/jusitos-0.0.1.img        — bootable raw disk image
#    build/kernel.bin               — raw kernel (for UEFI payload path)
#    build/rootfs/                  — populated rootfs tree
# ==========================================================================
set -euo pipefail

# ---- Config ----
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${PROJECT_ROOT}/build/freebsd-src"
OBJ_DIR="${PROJECT_ROOT}/build/obj"
ROOTFS_DIR="${PROJECT_ROOT}/build/rootfs"
IMG_DIR="${PROJECT_ROOT}/build"
IMG_FILE="${IMG_DIR}/jusitos-0.0.1.img"

FREEBSD_BRANCH="releng/14.2"
TARGET_ARCH="arm64"
KERNCONF="JusITOS-ARM64"

# ---- Sanity ----
echo "[jusit] project root : $PROJECT_ROOT"
echo "[jusit] freebsd branch: $FREEBSD_BRANCH"
echo "[jusit] target arch   : $TARGET_ARCH"
echo "[jusit] kernel config : $KERNCONF"
echo

# ---- Step 1 — clone FreeBSD source if absent ----
if [ ! -d "$SRC_DIR/.git" ]; then
    echo "[1/6] cloning freebsd-src ($FREEBSD_BRANCH) ..."
    mkdir -p "$(dirname "$SRC_DIR")"
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

# ---- Step 3 — build kernel ----
echo "[3/6] building kernel (this is the long step) ..."
cd "$SRC_DIR"
export MAKEOBJDIRPREFIX="$OBJ_DIR"
export TARGET=arm64
export TARGET_ARCH=arm64
export KERNCONF="$KERNCONF"

make -j"$(nproc)" buildkernel

# ---- Step 4 — build minimal userland (very small set) ----
echo "[4/6] building minimal userland ..."
# We only need the bits the init program touches:
#   /bin/sh, /sbin/init (we replace it), /lib/libc, mount utilities, etc.
make -j"$(nproc)" buildworld NO_KERBEROS=YES NO_OPENSSH=YES \
                              NO_SENDMAIL=YES NO_NIS=YES \
                              NO_PROFILE=YES NO_BIND=YES

# ---- Step 5 — assemble rootfs ----
echo "[5/6] assembling rootfs at $ROOTFS_DIR ..."
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR"/{bin,sbin,etc,dev,proc,boot,lib,usr/local/bin,root,mnt,tmp}

# Install kernel
make installkernel DESTDIR="$ROOTFS_DIR"

# Install world — minimal subset
make installworld DESTDIR="$ROOTFS_DIR" NO_KERBEROS=YES NO_OPENSSH=YES \
                                    NO_SENDMAIL=YES NO_NIS=YES \
                                    NO_PROFILE=YES NO_BIND=YES

# Install our init binary (built separately — see build_init.sh)
if [ -f "${PROJECT_ROOT}/build/jusit-init" ]; then
    cp "${PROJECT_ROOT}/build/jusit-init" "$ROOTFS_DIR/sbin/init"
    chmod 755 "$ROOTFS_DIR/sbin/init"
    cp "${PROJECT_ROOT}/build/jusit-init" "$ROOTFS_DIR/bin/jusit-init"
else
    echo "[jusit] WARNING: jusit-init not built; run scripts/build_init.sh first."
fi

# rc.conf — minimal, console-only
cat > "$ROOTFS_DIR/etc/rc.conf" <<'EOF'
hostname="jusitos"
ifconfig_DEFAULT="DHCP"
sshd_enable="NO"
syslogd_enable="NO"
sendmail_enable="NONE"
# Boot straight to our splash — no normal multi-user.
# init(8) calls /sbin/init, which is our binary, which paints the splash.
EOF

# fstab — single root partition
cat > "$ROOTFS_DIR/etc/fstab" <<'EOF'
/dev/vtbd0s1a / ufs rw 1 1
proc /proc procfs rw 0 0
EOF

# /etc/ttys — disable getty; init takes over the console directly
cat > "$ROOTFS_DIR/etc/ttys" <<'EOF'
console "/sbin/init" vt100 on secure
EOF

# ---- Step 6 — pack image ----
echo "[6/6] packing disk image ..."
IMG_SIZE_MB=256
truncate -s "${IMG_SIZE_MB}M" "$IMG_FILE"

# Single UFS root on a GPT partition (no EFI yet — UEFI payload comes later)
gpart create -s GPT "$IMG_FILE"
gpart add -t freebsd-ufs -s "$((IMG_SIZE_MB - 8))M" "$IMG_FILE"

# Format and populate UFS — done via mdconfig on FreeBSD
MDUNIT=$(mdconfig -a -t vnode -f "$IMG_FILE")
gpart create -s GPT "/dev/$MDUNIT"
gpart add -t freebsd-ufs -s "$((IMG_SIZE_MB - 8))M" "/dev/$MDUNIT"
newfs -U "/dev/${MDUNIT}p1"

MOUNTPOINT=$(mktemp -d)
mount "/dev/${MDUNIT}p1" "$MOUNTPOINT"
cd "$ROOTFS_DIR"
tar -cf - . | tar -xf - -C "$MOUNTPOINT"
cd -
umount "$MOUNTPOINT"
mdconfig -d -u "$MDUNIT"
rmdir "$MOUNTPOINT" || true

echo
echo "[jusit] BUILD COMPLETE"
echo "        image: $IMG_FILE"
echo "        size : ${IMG_SIZE_MB} MB"
echo "        kernel: $(ls -la $OBJ_DIR/*/usr/src/sys/$TARGET_ARCH/conf/$KERNCONF/build/kernel.bin 2>/dev/null || echo 'see obj tree')"
