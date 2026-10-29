# JusITOS Roadmap

## Where we are

**v0.0.1 — initial boot, framebuffer splash, keypress echo.**
Code is written; first real test happens when the build host is ready.

## Milestones

| Version   | Theme                       | Done when                                                           |
|-----------|-----------------------------|---------------------------------------------------------------------|
| **0.0.1** | "It boots and shows"        | Splash screen + keypress echo works in QEMU                         |
| 0.0.2     | "It loads a game"           | Game loader picks a binary from `/games/` and execs it full-screen  |
| 0.0.3     | "Gamepad"                   | Real USB HID gamepad driver; D-pad / buttons replace keyboard        |
| 0.0.4     | "Audio"                     | `snd_*` in kernel, init plays startup chime, games can play SFX      |
| 0.0.5     | "GPU 2D"                    | virtio-gpu + 2D blit API; software cursor                           |
| 0.1.0     | "Graphics API"              | Decision: Mesa swrast OpenGL OR minimal Vulkan loader                |
| 0.2.0     | "Real hardware"             | Boot on a Raspberry Pi 4 / 5 board (still framebuffer)               |
| 0.3.0     | "Storage layer"             | Signed-game package format; install/remove flow                    |
| 0.4.0     | "Network"                   | Online game catalog; OTA system updates                             |
| 1.0.0     | "First real release"        | Boots on real hardware, runs at least 3 demo games, audio + input    |

## Architecture decisions deferred to later milestones

- **Init system**: keep custom init forever, OR migrate to a small launchd-style supervisor at 0.2.0?
- **Package format**: TAR+signature? Custom container? APT-style?
- **Graphics API**: OpenGL ES (Mesa, GPL/LGPL territory) vs Vulkan (more permissive loader, but more code)?
- **Real hardware target**: Raspberry Pi (cheap, available) vs a custom board (more control, more work)?

We will pick these once 0.0.1 actually boots and we've felt the pain points.
