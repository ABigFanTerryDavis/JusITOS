#!/usr/bin/env python3
"""Boot JusITOS and require a framebuffer splash plus clean guest poweroff."""

from __future__ import annotations

import argparse
import json
import os
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

INIT_MARKER = "[JusITOS] INIT_REACHED"
SPLASH_MARKER = "[JusITOS] SPLASH_READY"
POWEROFF_MARKER = "[JusITOS] POWEROFF_REQUESTED"
FRAMEBUFFER_FALLBACK_MARKER = "framebuffer splash unavailable."
PANIC_RE = re.compile(
    r"(?:panic:|fatal trap|fatal kernel mode|\[JusITOS\] fatal:)", re.IGNORECASE
)


def connect_tcp(port: int, process: subprocess.Popen[bytes], deadline: float) -> socket.socket:
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited before its serial socket connected (status {process.returncode})")
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.settimeout(0.25)
        try:
            client.connect(("127.0.0.1", port))
            client.settimeout(None)
            client.setblocking(False)
            return client
        except OSError:
            client.close()
            time.sleep(0.1)
    raise TimeoutError("QEMU serial socket did not become available")


def connect_qmp(path: Path, process: subprocess.Popen[bytes], deadline: float) -> socket.socket:
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited before its QMP socket connected (status {process.returncode})")
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.settimeout(0.25)
        try:
            client.connect(str(path))
            client.settimeout(2.0)
            return client
        except OSError:
            client.close()
            time.sleep(0.1)
    raise TimeoutError("QEMU QMP socket did not become available")


def free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def qmp_send(sock: socket.socket, message: dict[str, Any]) -> None:
    sock.sendall((json.dumps(message) + "\r\n").encode("ascii"))


def require_file(path: Path, label: str) -> Path:
    path = path.expanduser().resolve()
    if not path.is_file() or path.stat().st_size == 0:
        raise FileNotFoundError(f"{label} is missing or empty: {path}")
    return path


def run(args: argparse.Namespace) -> int:
    image = require_file(args.image, "disk image")
    firmware_code = require_file(args.uefi_code, "UEFI code firmware")
    firmware_vars = require_file(args.uefi_vars, "UEFI variables template")
    log_path = args.log.expanduser().resolve()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    qemu_stderr_path = log_path.with_name("qemu.stderr.log")

    qemu = shutil.which(args.qemu)
    if qemu is None:
        raise FileNotFoundError(f"QEMU executable not found: {args.qemu}")

    temp_dir = Path(tempfile.mkdtemp(prefix="jusitos-qemu-"))
    vars_copy = temp_dir / "vars.fd"
    qmp_path = temp_dir / "qmp.sock"
    shutil.copyfile(firmware_vars, vars_copy)
    serial_port = free_tcp_port()

    command = [
        qemu,
        "-accel", "tcg",
        "-machine", "virt,gic-version=3",
        "-cpu", "cortex-a57",
        "-smp", "2",
        "-m", "2048",
        "-drive", f"if=pflash,format=raw,unit=0,file={firmware_code},readonly=on",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}",
        "-display", "none",
        "-device", "ramfb",
        "-serial", f"tcp:127.0.0.1:{serial_port},server=on,wait=on",
        "-monitor", "none",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
        "-drive", f"if=none,file={image},format=raw,id=hd0",
        "-device", "virtio-blk-pci,drive=hd0",
        "-device", "qemu-xhci,id=xhci",
        "-device", "usb-kbd,bus=xhci.0",
        "-device", "usb-mouse,bus=xhci.0",
        "-device", "virtio-net-pci,netdev=net0",
        "-netdev", "user,id=net0",
        "-device", "virtio-rng-pci",
        "-rtc", "base=utc",
        "-no-reboot",
        "-no-shutdown",
    ]

    print("Starting AArch64 QEMU with TCG; waiting for the guest serial console.", flush=True)
    qemu_stderr = qemu_stderr_path.open("wb")
    process: subprocess.Popen[bytes] | None = None
    serial: socket.socket | None = None
    qmp: socket.socket | None = None
    result = 1
    try:
        with log_path.open("wb") as serial_log:
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=qemu_stderr)
            start = time.monotonic()
            boot_deadline = start + args.boot_timeout
            serial = connect_tcp(serial_port, process, boot_deadline)
            qmp = connect_qmp(qmp_path, process, boot_deadline)
            qmp_buffer = bytearray()
            serial_text = ""
            init_seen = False
            splash_seen = False
            ctrl_d_sent = False
            poweroff_seen = False
            guest_shutdown_seen = False
            qmp_ready = False
            quit_sent = False
            shutdown_deadline: float | None = None
            shutdown_serial_deadline: float | None = None
            exit_deadline: float | None = None

            while True:
                status = process.poll()
                if status is not None:
                    if (
                        status == 0 and init_seen and splash_seen and ctrl_d_sent and poweroff_seen
                        and guest_shutdown_seen and quit_sent
                    ):
                        print("PASS: JusITOS drew the framebuffer splash; Ctrl+D requested poweroff; QEMU reported guest-shutdown and exited 0.", flush=True)
                        result = 0
                        break
                    raise RuntimeError(
                        "QEMU exited before all boot/shutdown assertions passed "
                        f"(status {status}; init={init_seen}, splash={splash_seen}, ctrl_d={ctrl_d_sent}, "
                        f"poweroff_marker={poweroff_seen}, guest_shutdown={guest_shutdown_seen})"
                    )

                now = time.monotonic()
                if guest_shutdown_seen:
                    if not poweroff_seen and shutdown_serial_deadline is not None and now >= shutdown_serial_deadline:
                        raise TimeoutError(
                            "QMP reported guest-shutdown, but the serial POWEROFF_REQUESTED marker "
                            f"did not arrive within {args.serial_shutdown_grace:g} seconds"
                        )
                    if quit_sent and exit_deadline is not None and now >= exit_deadline:
                        raise TimeoutError("QEMU did not exit cleanly after QMP quit")
                elif splash_seen:
                    if shutdown_deadline is not None and now >= shutdown_deadline:
                        raise TimeoutError("guest did not request and complete poweroff before the shutdown timeout")
                elif now >= boot_deadline:
                    raise TimeoutError(f"JusITOS SPLASH_READY marker not observed within {args.boot_timeout} seconds")

                active_deadline = boot_deadline
                if splash_seen and not guest_shutdown_seen and shutdown_deadline is not None:
                    active_deadline = shutdown_deadline
                if guest_shutdown_seen and not poweroff_seen and shutdown_serial_deadline is not None:
                    active_deadline = shutdown_serial_deadline
                if guest_shutdown_seen and poweroff_seen and not quit_sent:
                    active_deadline = now + 0.25
                if quit_sent and exit_deadline is not None:
                    active_deadline = exit_deadline
                wait = max(0.0, min(0.25, active_deadline - now))
                readers: list[socket.socket] = []
                if serial is not None:
                    readers.append(serial)
                if qmp is not None:
                    readers.append(qmp)
                ready, _, _ = select.select(readers, [], [], wait)

                for source in ready:
                    data = source.recv(65536)
                    if not data:
                        if source is serial:
                            serial.close()
                            serial = None
                            if not guest_shutdown_seen:
                                raise RuntimeError("QEMU serial connection closed before guest shutdown")
                        else:
                            qmp.close()
                            qmp = None
                            if not quit_sent:
                                raise RuntimeError("QEMU QMP connection closed before the controlled quit")
                        continue

                    if source is serial:
                        serial_log.write(data)
                        serial_log.flush()
                        serial_text += data.decode("utf-8", errors="replace")
                        if len(serial_text) > 8_000_000:
                            serial_text = serial_text[-4_000_000:]
                        if PANIC_RE.search(serial_text):
                            raise RuntimeError("panic/fatal marker found in the guest serial log")
                        if not splash_seen and FRAMEBUFFER_FALLBACK_MARKER in serial_text:
                            raise RuntimeError(
                                "guest fell back to the serial console; this test requires the framebuffer SPLASH_READY marker"
                            )
                        if not init_seen and INIT_MARKER in serial_text:
                            init_seen = True
                            print("Observed [JusITOS] INIT_REACHED; waiting for the framebuffer splash marker.", flush=True)
                        if not splash_seen and SPLASH_MARKER in serial_text:
                            if not init_seen:
                                raise RuntimeError("SPLASH_READY appeared without the expected INIT_REACHED marker")
                            splash_seen = True
                            shutdown_deadline = time.monotonic() + args.shutdown_timeout
                            print("Observed [JusITOS] SPLASH_READY; sending Ctrl+D on the guest UART.", flush=True)
                            if serial is None:
                                raise RuntimeError("serial connection disappeared before Ctrl+D could be sent")
                            serial.sendall(b"\x04")
                            ctrl_d_sent = True
                        if splash_seen and ctrl_d_sent and not poweroff_seen and POWEROFF_MARKER in serial_text:
                            poweroff_seen = True
                            print("Observed [JusITOS] POWEROFF_REQUESTED.", flush=True)
                    else:
                        qmp_buffer.extend(data)
                        while b"\n" in qmp_buffer:
                            raw_line, _, remainder = qmp_buffer.partition(b"\n")
                            qmp_buffer = bytearray(remainder)
                            if not raw_line.strip():
                                continue
                            message = json.loads(raw_line)
                            if "QMP" in message and not qmp_ready:
                                qmp_send(qmp, {"execute": "qmp_capabilities", "id": "capabilities"})
                                continue
                            if message.get("id") == "capabilities":
                                if "error" in message:
                                    raise RuntimeError(f"QMP capability negotiation failed: {message['error']}")
                                qmp_ready = True
                                continue
                            if message.get("event") == "SHUTDOWN":
                                event_data = message.get("data", {})
                                reason = event_data.get("reason", "unspecified")
                                guest = event_data.get("guest", False)
                                print(f"QMP SHUTDOWN event: guest={guest}, reason={reason}.", flush=True)
                                if not guest or reason != "guest-shutdown":
                                    raise RuntimeError(f"QEMU reported an unclean/non-guest shutdown: {event_data}")
                                if not guest_shutdown_seen:
                                    guest_shutdown_seen = True
                                    shutdown_serial_deadline = time.monotonic() + args.serial_shutdown_grace
                                    shutdown_deadline = None
                            elif message.get("event") == "RESET":
                                raise RuntimeError(f"unexpected guest reset during boot test: {message.get('data', {})}")

                # QMP and serial are separate transports. Let serial catch up
                # after SHUTDOWN rather than assuming an unseen marker is absent.
                if guest_shutdown_seen and poweroff_seen and qmp_ready and not quit_sent:
                    qmp_send(qmp, {"execute": "quit", "id": "quit"})
                    quit_sent = True
                    exit_deadline = time.monotonic() + 10

        return result
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if serial is not None:
            serial.close()
        if qmp is not None:
            qmp.close()
        qemu_stderr.close()
        shutil.rmtree(temp_dir, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--uefi-code", type=Path, required=True)
    parser.add_argument("--uefi-vars", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-aarch64"))
    parser.add_argument("--boot-timeout", type=int, default=900)
    parser.add_argument("--shutdown-timeout", type=int, default=120)
    parser.add_argument("--serial-shutdown-grace", type=float, default=2.0)
    args = parser.parse_args()
    if args.boot_timeout < 1 or args.shutdown_timeout < 1 or args.serial_shutdown_grace <= 0:
        parser.error("boot/shutdown timeouts and serial shutdown grace must be positive")
    try:
        return run(args)
    except Exception as error:  # Print a concise failure while leaving logs for the workflow artifact.
        print(f"FAIL: {error}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
