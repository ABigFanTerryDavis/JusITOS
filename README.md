# JusITOS 0.0.1

A FreeBSD-derived ARM64 game-console operating system. Target: small, fast-booting, framebuffer-only console that boots a splash screen and reads input. QEMU is the primary test platform.

> Lineage: FreeBSD 14.2-RELEASE → stripped console profile → custom kernel + custom init = **JusITOS 0.0.1**.
> This follows the same pattern Sony used for Orbis OS (PS4) and Prospero (PS5), both of which are FreeBSD-derived.

---

## What's in 0.0.1

| Piece                          | Status      | File / location                          |
|--------------------------------|-------------|------------------------------------------|
| Custom AArch64 kernel config   | ✅ written  | `src/sys/arm64/conf/JusITOS-ARM64`       |
| Splash / init binary           | ✅ written  | `src/init/jusit_init.c`                   |
| Build script (src + image)     | ✅ written  | `scripts/build.sh`                       |
| Init binary builder            | ✅ written  | `scripts/build_init.sh`                   |
| QEMU launcher                   | ✅ written  | `scripts/run_qemu.sh`                     |
| Device-tree fragment           | ✅ written  | `src/devtree/jusit-virt.dts`              |
| Boot to splash screen          | 🎯 target  | Once you actually build + run             |
| Keypress echo on framebuffer   | 🎯 target  | Once you actually build + run             |

### What 0.0.1 does NOT do (by design)

- No GPU acceleration (framebuffer only)
- No audio
- No real gamepad driver (keyboard-as-input for now)
- No networking (driver is built, not used yet)
- No game loader (one splash screen)
- No UEFI secure boot

These come in later milestones (see roadmap below).

---

## Prerequisites

You need **one** of these build hosts:

1. **FreeBSD 13.4+ or 14.x arm64 host** (preferred — easiest path)
2. **FreeBSD VM** on your dev box (e.g., UTM on macOS, bhyve on Linux)
3. **Linux host with a FreeBSD arm64 cross-toolchain** — more work, see `scripts/build_init.sh` notes

Plus:

- ~10 GB free disk (for src + objtree)
- `git`, `make`, `cc` (clang preferred)
- `qemu-system-aarch64` (for running the image)
- `dtc` (device tree compiler, optional — `apt install device-tree-compiler`)

---

## Build

```bash
# 1. Make scripts executable
chmod +x scripts/*.sh

# 2. (Skip if on FreeBSD arm64 natively) Build the init binary
./scripts/build_init.sh

# 3. Build everything (clones freebsd-src, builds kernel + world, packs image)
./scripts/build.sh
```

The long step is `buildkernel` (5–30 min depending on machine). The full world build is also long but only needs to happen once.

Output:

- `build/jusitos-0.0.1.img` — bootable raw disk image (256 MB)
- `build/rootfs/` — populated rootfs tree (for inspection / debugging)
- `build/jusit-init` — the splash/init binary

## Run

```bash
# Launch with display window (you'll see the splash screen appear)
./scripts/run_qemu.sh

# Or headless for CI / batch tests
./scripts/run_qemu.sh --headless

# Or with GDB stub for kernel debugging
./scripts/run_qemu.sh --debug
```

You should see:

1. QEMU opens a window.
2. UEFI firmware (if you have OVMF) or direct kernel boot loads.
3. FreeBSD kernel boot messages scroll on serial (stdout).
4. The kernel hands off to `/sbin/init` → our `jusit-init` binary.
5. The framebuffer fills with the JusITOS splash screen.
6. Pressing a key echoes that key at the bottom of the screen.
7. Ctrl+D triggers clean poweroff.

---

## Project structure

```
JusITOS/
├── README.md                          ← you are here
├── src/
│   ├── sys/arm64/conf/JusITOS-ARM64   ← custom kernel config
│   ├── init/jusit_init.c              ← splash + init program
│   └── devtree/jusit-virt.dts         ← device tree fragment
├── scripts/
│   ├── build.sh                       ← clone + build kernel + world + image
│   ├── build_init.sh                  ← cross-compile the init binary
│   └── run_qemu.sh                    ← launch in QEMU
├── rootfs/                            ← populated by build script
├── build/                             ← build outputs (freebsd-src clone, obj, image)
└── docs/
    └── ROADMAP.md                     ← milestones M1 → M5+
```

---

## Roadmap

### ✅ v0.0.1 — "It boots and shows something"
- Custom stripped FreeBSD arm64 kernel
- Splash screen on framebuffer
- Keyboard input echo
- QEMU virt target

### 🔜 v0.0.2 — "It loads a game"
- Add `/games/` partition
- Init binary gains a game-loader: scan `/games/`, list on screen, run selection
- Refactor init into a launcher shell (still pixel-only output)

### 🔜 v0.0.3 — "Gamepad input"
- Write `jusitkbd` driver for USB HID gamepads (no event device, just custom ioctl)
- Replace keyboard echo with button-press echo

### 🔜 v0.0.4 — "Audio"
- Enable `snd_*` drivers in kernel config
- Expose a simple `/dev/jusit-snd` PCM device
- Init plays a startup chime

### 🔜 v0.0.5 — "GPU"
- Bring in `drm-kmod` (virtio-gpu)
- Add a tiny software-rendering 2D API on top of framebuffer + GPU 2D blits

### 🔜 v0.1.0 — "Real graphics API"
- Pick a path: software OpenGL (Mesa swrast) or minimal Vulkan loader
- Game-side API spec for v1.0

---

## Known limitations of 0.0.1

1. **The init binary uses 5×7 hand-rolled font** — minimal ASCII only. Will be replaced with a proper font in 0.0.2.
2. **`reboot(RB_POWEROFF)` syscall constant may need adjusting** for FreeBSD arm64 — check `/usr/include/sys/reboot.h` on your build host and update if needed.
3. **QEMU framebuffer geometry is hardcoded to 640×480 in the DT fragment** but the init binary queries it at runtime, so this is safe.
4. **No secure boot / signed games** — anyone who can write to the disk can replace the init binary.
5. **No real console driver** — we use `/dev/fb0` directly. vt(4) is built but not used by init in 0.0.1.

---

## License

JusITOS is licensed under the **BSD 2-Clause License**, matching FreeBSD. All FreeBSD-derived code remains under its original copyright. See `LICENSE` (to be added before public release).

---

## Next steps for the team

1. Set up a FreeBSD arm64 build host (UTM VM is fastest if you're on macOS).
2. Run `scripts/build.sh` end-to-end once — fix any environment issues.
3. Run `scripts/run_qemu.sh` — confirm the splash screen appears.
4. Press a key — confirm it echoes.
5. Report back what worked / didn't, and we'll iterate on 0.0.2.
