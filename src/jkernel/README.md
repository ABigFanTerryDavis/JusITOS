# jkernel milestone 0.3: ramfb display and zesh serial monitor

`src/jkernel/` is an independent freestanding AArch64 kernel experiment for QEMU's `virt` machine. It does not replace or modify the FreeBSD 14.2 prototype and is not wired into the FreeBSD build or its QEMU scripts.

## Current scope

The kernel enters at `_start`, establishes a private stack, and keeps the existing PL011-backed `zesh>` serial monitor. `zesh` polls a nonblocking character-input callback, echoes printable input, supports Backspace and DEL, accepts CR or LF as Enter, and provides `help` and `version`. Its line buffer holds up to 127 characters plus a terminator; an overlong command is discarded rather than running a prefix. `zesh` remains an in-kernel monitor, not a user process.

The first display slice parses the QEMU-provided FDT to locate `qemu,fw-cfg-mmio`, checks fw_cfg's signature and DMA feature, finds the `etc/ramfb` file entry, and configures QEMU ramfb through a fw_cfg DMA write. The screen is a statically allocated 640x480 XRGB8888 framebuffer drawn entirely in software: color bars, a dark console panel, and a small bitmap-font status display.

This direct-boot path uses QEMU 8.2.2's AArch64 ELF boot behavior: the generated DTB is placed at RAM base `0x40000000` only if it fits below the first ELF load address. The linker starts the first `PT_LOAD` at `0x40200000`, leaving 2 MiB for the DTB. `start.S` repurposes `x0`, so the kernel intentionally does not depend on an `x0` DTB pointer. The FDT helper has no independent blob-length argument; the caller must ensure the pointer spans the total size in the FDT header. These assumptions are specific to QEMU 8.2.2 direct-ELF boot, not a general boot protocol.

The 640x480 display, UI cursor, polled VirtIO tablet input, and serial shell passed a QEMU 8.2.2 GTK smoke test. The kernel reads normalized pointer snapshots and redraws the UI when pointer or button state changes. This verifies the QEMU sandbox path only; physical hardware has not been tested. Controller passthrough, accelerated graphics, a general graphics API, virtual memory, interrupts, storage, and scheduling are not included.

## Build

Use an AArch64 freestanding GCC toolchain (the default is `aarch64-none-elf-gcc`). For a differently prefixed cross-compiler, set `CROSS_COMPILE`, for example:

```sh
make -C src/jkernel CROSS_COMPILE=aarch64-linux-gnu-
```

The ELF output is `build/jkernel/jkernel.elf`, outside the FreeBSD prototype's named image and source outputs. `make -C src/jkernel clean` removes only that dedicated output directory.

## QEMU launch

The display-enabled direct-boot command includes `virtio-tablet-device` so the polled input driver can discover QEMU's tablet among the VirtIO-MMIO transports:

```sh
qemu-system-aarch64 \
  -machine virt,gic-version=3 \
  -cpu cortex-a57 -smp 1 -m 2048 \
  -global virtio-mmio.force-legacy=false \
  -display gtk -device ramfb -device virtio-tablet-device -serial stdio \
  -kernel build/jkernel/jkernel.elf
```

The `force-legacy=false` setting selects the modern VirtIO-MMIO version 2 transport required by the tablet driver; QEMU 8.2.2 defaults this property to legacy mode. The visible smoke test confirmed the 640x480 UI, cursor movement, left-button state, and serial `help`/`version` responses. QEMU emitted a missing `vgabios-ramfb.bin` warning, but it did not prevent the fw_cfg ramfb display from working. Use `-display none` instead of `-display gtk` for a headless run; ramfb remains attached, but its pixels will not be shown in a host window. This is separate from `scripts/run_qemu.sh`, which launches the FreeBSD disk image.
