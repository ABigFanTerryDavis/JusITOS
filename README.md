# JusITOS 0.0.1

A FreeBSD-derived ARM64 game-console OS prototype. The intended v0.0.1 experience is a fast-booting console with a splash screen and keyboard input. QEMU's `virt` machine is the development target.

> Source baseline: FreeBSD `releng/14.2`, plus the JusITOS kernel configuration and init program.

## Current implementation status

| Piece | Status | Location |
|---|---|---|
| Custom AArch64 kernel config | Updated; not yet built | `src/sys/arm64/conf/JusITOS-ARM64` |
| Splash/init program | Host syntax checked; target runtime unverified | `src/init/jusit_init.c` |
| FreeBSD source, kernel, and world build | Scripted for a FreeBSD host; not yet run | `scripts/build.sh` |
| UEFI raw image | GPT/ESP/UFS assembly mock-tested; guest boot unverified | `scripts/build.sh` |
| QEMU launcher | Arguments mock-tested; guest boot unverified | `scripts/run_qemu.sh` |
| Device-tree fragment | Informational; not passed by the runner | `src/devtree/jusit-virt.dts` |

The splash supports a narrow UEFI framebuffer target: init requires `FBTYPE_EFIFB` at 32-bit depth, reads the scanline width with `FBIO_GETLINEWIDTH`, and queries RGB shifts with `FBIO_GETRGBOFFS` on `/dev/ttyv0`. The current pixel layout must report R=16, G=8, and B=0; other framebuffer types, depths, or channel layouts are not supported. When the EFI framebuffer is missing or unsupported, init falls back to `/dev/console`, the system-selected console, which may be serial. As PID 1, init opens `/dev/console` on file descriptors 0, 1, and 2 in raw mode. Ctrl+D is the byte `0x04` and requests poweroff with `RB_POWEROFF`. The font fallback and glyphs are visible when rendered.

## Build host and prerequisites

The full `scripts/build.sh` image build requires a **FreeBSD 14.2-or-newer host** and root privileges. The build target is `arm64/aarch64`; it can be built natively on FreeBSD/arm64 or cross-built from FreeBSD/amd64 with the FreeBSD toolchain. The script uses FreeBSD-specific `mdconfig`, `gpart`, `newfs_msdos`, and `mount` commands, so **Linux is not supported for the full build/image step**.

Linux or another Clang host can build only the standalone init executable when given a matching FreeBSD 14.2 arm64 sysroot. That does not make the full image builder Linux-compatible; transfer the resulting `build/jusit-init` to the FreeBSD build host before running `build.sh`.

You will also need Git, `make`, Clang/C compiler tools, roughly 10 GB of free space for source and build outputs, and `qemu-system-aarch64` plus AArch64 UEFI firmware for running the result. The runner auto-detects common monolithic firmware paths and AAVMF/EDK2 code-vars pairs. Set `UEFI_FIRMWARE` for a single-file image, or set both `UEFI_CODE` and `UEFI_VARS` for a split pflash installation.

## Build

On FreeBSD/arm64, `build.sh` automatically builds `jusit-init` before starting the OS build. On FreeBSD/amd64, cross-build it with a suitable sysroot first; `build.sh` stops early if the binary is missing:

```sh
# Required on a cross-build host before running build.sh.
JUSIT_SYSROOT=/path/to/freebsd-14.2-arm64-sysroot ./scripts/build_init.sh

# Builds FreeBSD 14.2, stages the root filesystem, and creates the image.
sudo ./scripts/build.sh
```

The first run clones the FreeBSD `releng/14.2` source branch and builds the kernel and world. The raw image size is calculated from the staged root filesystem, with space reserved for the EFI partition and UFS overhead; it is no longer assumed to fit in a 256 MiB disk. To request a larger image, set `IMG_SIZE_MB` (the script rejects a size too small for the staged files).

Outputs:

- `build/jusitos-0.0.1.img` — raw GPT disk image, with a FAT EFI System Partition and UFS root.
- `build/rootfs/` — staged root filesystem for inspection.
- `build/jusit-init` — the AArch64 init executable installed into the image.

## Run in QEMU

The boot path uses AArch64 UEFI firmware and the standard removable-media loader path. The ESP contains FreeBSD's `boot/loader.efi` as `EFI/BOOT/BOOTAA64.EFI` and `EFI/FreeBSD/loader.env` with `rootdev=disk0p2`; `loader.efi` reads that setting before it can load `loader.conf` from UFS. The UFS root is GPT partition 2 and appears as `/dev/vtbd0p2` with the runner's PCI virtio disk. Once it reaches UFS, `boot/loader.conf` supplies `vfs.root.mountfrom`. QEMU's `ramfb` device exposes the UEFI GOP framebuffer in the BGRx layout init expects; `--headless` turns off host display output but keeps `ramfb` attached.

```sh
# Single-file firmware example; auto-detection also checks common locations.
UEFI_FIRMWARE=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd ./scripts/run_qemu.sh

# Split AAVMF firmware example (the vars template is copied to a temporary file).
UEFI_CODE=/usr/share/AAVMF/AAVMF_CODE.fd \
UEFI_VARS=/usr/share/AAVMF/AAVMF_VARS.fd ./scripts/run_qemu.sh --headless

# Headless serial output, or start paused with the GDB stub.
./scripts/run_qemu.sh --headless
./scripts/run_qemu.sh --debug
```

The runner requires `qemu-system-aarch64` and valid AArch64 UEFI firmware; it stops with an error rather than silently attempting a non-UEFI boot. For split pflash firmware, it uses a temporary copy of the variable-store template and leaves the system template untouched. Expect firmware and kernel boot messages on the serial console, followed by the JusITOS init behavior described above. The source/image changes in this update have been exercised with mocked host tools, but an end-to-end FreeBSD guest boot has not yet been run.

## Project structure

```text
JusITOS/
├── README.md
├── src/
│   ├── sys/arm64/conf/JusITOS-ARM64
│   ├── init/jusit_init.c
│   └── devtree/jusit-virt.dts
├── scripts/
│   ├── build.sh
│   ├── build_init.sh
│   └── run_qemu.sh
├── rootfs/
├── build/
└── docs/ROADMAP.md
```

## Roadmap

- **v0.0.1:** bootable FreeBSD arm64 base, framebuffer splash, keyboard input.
- **v0.0.2:** game partition and game selection/launch.
- **v0.0.3:** USB gamepad input.
- **v0.0.4:** audio.
- **v0.0.5:** accelerated graphics.
- **v0.1.0:** graphics API and game-side specification.

## License

JusITOS is intended to use the BSD 2-Clause License, matching FreeBSD. FreeBSD-derived code remains under its original copyright. Add the project's `LICENSE` file before release.
