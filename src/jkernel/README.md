# jkernel milestone 0.2: zesh serial monitor

This directory is an independent freestanding AArch64 kernel experiment for QEMU's `virt` machine. It does not replace or modify the FreeBSD 14.2 prototype, and it is not wired into the existing FreeBSD build or QEMU scripts.

## Current scope

The kernel enters at `_start`, establishes a private stack, initializes QEMU `virt`'s PL011 UART, and runs `zesh` as a small in-kernel serial bring-up monitor. The monitor polls a nonblocking character-input callback and keeps its line state between calls. It prints a `zesh>` prompt, echoes printable input, supports Backspace and DEL editing, and accepts CR or LF as Enter. Its built-in commands are `help` and `version` (`zesh 0.1`).

The input buffer holds up to 127 characters plus a terminator. If a line overflows, `zesh` rings the bell, discards the entire line on Enter, and does not run the buffered prefix. The callback interface keeps the monitor's line handling separate from the PL011 driver, so it can later move into Jusland if user mode and system calls are added.

For now, `zesh` runs inside the kernel; it is not a user process. This milestone does not include COIO, user mode or system calls, a device-tree parser, virtual memory, exception handling, interrupts or the GIC, storage, or a scheduler.

The earlier banner-only jkernel milestone was built and directly booted under QEMU. The current zesh-integrated milestone has not yet been built or booted, so its interactive input path is not verified.

## Build

Use an AArch64 freestanding GCC toolchain (the default command is `aarch64-none-elf-gcc`). For a differently prefixed cross-compiler, set `CROSS_COMPILE`, for example:

```sh
make -C src/jkernel CROSS_COMPILE=aarch64-linux-gnu-
```

The ELF output is `build/jkernel/jkernel.elf`, outside the FreeBSD prototype's named image and source outputs. Run `make -C src/jkernel clean` to remove only that dedicated output directory.

## Proposed QEMU smoke test

After the current source review is complete and any requested fixes are made, the kernel can be built and tested directly on QEMU's single-core `virt` machine:

```sh
make -C src/jkernel
qemu-system-aarch64 \
  -machine virt,gic-version=3 \
  -cpu cortex-a57 -smp 1 -m 2048 \
  -display none -serial stdio \
  -kernel build/jkernel/jkernel.elf
```

This is the proposed next check, not a claim that milestone 0.2 has built or booted. The existing `scripts/run_qemu.sh` launches the separate FreeBSD disk image and is not the jkernel runner.
