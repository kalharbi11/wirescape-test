# SPDX-License-Identifier: GPL-3.0-only
"""Windows CI smoke: build only, then interrupt a loopback-only Studio dev server."""
import ctypes
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

import wstest


def smoke(modules: list[str], root: Path = wstest.ROOT, run=subprocess.run,
          console_flag=None) -> None:
    for name in modules:
        if not wstest.NAME_RE.fullmatch(name) or not (root / "modules" / name / "module.cpp").is_file():
            raise ValueError(f"invalid scoped module: {name}")
    if console_flag is None:
        if os.name != "nt":
            raise RuntimeError("Windows app smoke requires a real Windows runner")
        console_flag = subprocess.CREATE_NEW_CONSOLE
        for tool in ("npm", "npx"):
            resolved = Path(wstest.tool_path(tool))
            if resolved.suffix.lower() != ".cmd":
                raise RuntimeError(f"{tool}: expected a Windows .cmd shim")
            print(f"Windows app smoke: resolved {resolved.name}", flush=True)
    for name in modules:
        if not (root / "modules" / name / wstest.PANEL_FILE).is_file():
            print(f"Windows app smoke: {name} skipped (no panel.json)", flush=True)
            continue
        print(f"Windows app smoke: {name} (build, typecheck, Ctrl+C)", flush=True)
        command = [sys.executable, str(root / "tools" / "wstest.py")]
        run(command + ["app", name], cwd=root, check=True,
            env={**os.environ, "ELECTRON_SKIP_BINARY_DOWNLOAD": "1"})
        run(command + ["ui", name, "--check"], cwd=root, check=True)
        # Broadcast Ctrl+C only in the new console, never the runner's console.
        try:
            worker = run([sys.executable, str(root / "tools" / "windows_app_smoke.py"),
                          "--interrupt-worker", name], cwd=root, check=True,
                         creationflags=console_flag, timeout=900,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding="utf-8", errors="replace")
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            print(error.stdout or "Windows lifecycle worker produced no output", flush=True)
            raise
        else:
            if worker.stdout:
                print(worker.stdout, flush=True)


def listener_present(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


def require_private_console(kernel, worker: int, child: int, parent: int) -> None:
    """Fail closed before broadcasting to a console containing the runner."""
    capacity = 16
    while True:
        buffer = (ctypes.c_uint32 * capacity)()
        count = kernel.GetConsoleProcessList(buffer, capacity)
        if count == 0:
            raise RuntimeError("cannot enumerate isolated console processes")
        if count <= capacity:
            members = set(buffer[:count])
            break
        capacity = count
    if worker not in members or child not in members or parent in members:
        raise RuntimeError("Ctrl+C target console is not isolated from the smoke parent")


def interrupt_worker(name: str) -> None:
    """Run in a private Windows console. wstest shares it; Vite has its own process group."""
    if os.name != "nt":
        raise RuntimeError("Ctrl+C witness requires Windows")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    if not kernel.GetConsoleWindow():
        raise RuntimeError("isolated Windows console unavailable")
    # Reset any inherited ignore-Ctrl+C state before creating the Python child.
    if not kernel.SetConsoleCtrlHandler(None, False):
        raise ctypes.WinError(ctypes.get_last_error())
    print("Windows lifecycle: private console, Ctrl+C enabled for child", flush=True)
    port = wstest.free_port()
    # Read the log via a second handle: no pipe fill or blocking readline.
    with tempfile.TemporaryDirectory() as tmp:
        log_path = Path(tmp) / "ui.log"
        with log_path.open("wb") as output:
            proc = subprocess.Popen(
                [sys.executable, str(wstest.ROOT / "tools" / "wstest.py"),
                 "ui", name, "--port", str(port)], cwd=wstest.ROOT,
                stdout=output, stderr=subprocess.STDOUT)
            try:
                # Set the ignore flag AFTER spawning wstest, which must receive Ctrl+C.
                if not kernel.SetConsoleCtrlHandler(None, True):
                    raise ctypes.WinError(ctypes.get_last_error())
                opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
                deadline = time.monotonic() + 600
                while time.monotonic() < deadline:
                    if proc.poll() is not None:
                        raise RuntimeError(f"ui exited before readiness: {proc.returncode}")
                    try:
                        with opener.open(f"http://127.0.0.1:{port}/", timeout=2) as reply:
                            if (reply.status == 200 and b"<html" in reply.read().lower()
                                    and "Studio dev server:" in log_path.read_text(
                                        encoding="utf-8", errors="replace")):
                                break
                    except OSError:
                        pass
                    time.sleep(0.5)
                else:
                    raise RuntimeError("Studio loopback HTTP readiness timed out")
                require_private_console(kernel, os.getpid(), proc.pid, os.getppid())
                print("Windows lifecycle: Studio loopback HTTP ready; sending isolated Ctrl+C", flush=True)
                if not kernel.GenerateConsoleCtrlEvent(0, 0):  # CTRL_C_EVENT, this console only
                    raise ctypes.WinError(ctypes.get_last_error())
                if proc.wait(timeout=45) != 0:
                    raise RuntimeError(f"ui did not exit cleanly: {proc.returncode}")
                deadline = time.monotonic() + 10
                while listener_present(port) and time.monotonic() < deadline:
                    time.sleep(0.25)
                if listener_present(port):
                    raise RuntimeError("Studio listener survived Ctrl+C")
                log = log_path.read_text(encoding="utf-8", errors="replace")
                if "stopping the dev server" not in log or "taskkill: process tree stopped" not in log:
                    raise RuntimeError("missing wstest KeyboardInterrupt/taskkill witness")
                print("Windows lifecycle: Ctrl+C handled, taskkill tree stopped, loopback port closed", flush=True)
            finally:
                if proc.poll() is None:
                    try:
                        subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                                       check=True, timeout=30)
                    finally:
                        if proc.poll() is None:
                            proc.kill()
                        proc.wait(timeout=10)
                print(log_path.read_text(encoding="utf-8", errors="replace"), flush=True)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--interrupt-worker":
        interrupt_worker(sys.argv[2])
    else:
        smoke(sys.argv[1:])
