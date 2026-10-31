# COIO Rust: UEFI GOP status screen

This isolated Rust project is a minimal AArch64 UEFI application. It uses
`aarch64-unknown-uefi`, `no_std`, no dependencies, and no dynamic allocation.
On entry it opens the firmware's Graphics Output Protocol (GOP) and draws a
small status screen directly to its active framebuffer. If GOP setup or drawing
fails, the application prints a diagnostic and the EFI status to the UEFI text
console (or StandardError) and returns that failure status. A panic also emits a
short EFI diagnostic when a text console is available, then halts. The Rust
experiment remains separate from the C COIO experiment and `jkernel`.

## Build

Install the Rust target once, then build and stage the removable-media fallback
binary:

```sh
rustup target add aarch64-unknown-uefi
./src/coio-rust/build.sh
```

The Cargo target directory and staged ESP tree are both under `build/coio-rust/`.
The staged application is `build/coio-rust/esp/EFI/BOOT/BOOTAA64.EFI`.

## Run under QEMU/AAVMF

The launcher adds a `ramfb` device and opens a GTK display by default so the GOP
status screen is visible. Set `COIO_HEADLESS=1` to disable the QEMU display; the
serial console remains connected to the terminal. The launcher uses AAVMF's
separate read-only code image and writable vars store. It copies the vars
template to `build/coio-rust/AAVMF_VARS.fd` on first launch, preserving the
installed template. QEMU's directory-backed FAT drive exposes only the staged
Rust ESP tree to the guest; the existing C `jkernel` launcher and direct-QEMU
path are unaffected.

```sh
UEFI_CODE=/usr/share/AAVMF/AAVMF_CODE.fd \
UEFI_VARS=/usr/share/AAVMF/AAVMF_VARS.fd \
./src/coio-rust/run-qemu.sh
```

For headless operation:

```sh
COIO_HEADLESS=1 ./src/coio-rust/run-qemu.sh
```

Override `QEMU`, `UEFI_CODE`, and `UEFI_VARS` if the tools or firmware are
installed elsewhere. `COIO_VARS_FILE` can select another writable vars copy,
and the launcher can take extra QEMU arguments after its script name.
