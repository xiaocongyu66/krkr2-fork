#!/usr/bin/env python3
"""Verify the production SDL3 GL window with a synthetic TJS fixture under X11."""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


WIDTH, HEIGHT = 800, 480
COLORS = {
    "top_left": 0xFF0000,
    "top_right": 0x00FF00,
    "bottom_left": 0x0000FF,
    "bottom_right": 0xFFFF00,
    "black": 0x000000,
}

FIXTURE = r'''
try {
    global.win = new Window();
    win.setSize(800, 480);
    win.visible = true;
    global.root = new Layer(win, null);
    root.setImageSize(640, 480);
    root.setSize(640, 480);
    root.fillRect(0, 0, 320, 240, 0xFFFF0000);
    root.fillRect(320, 0, 320, 240, 0xFF00FF00);
    root.fillRect(0, 240, 320, 240, 0xFF0000FF);
    root.fillRect(320, 240, 320, 240, 0xFFFFFF00);
    root.fillRect(97, 63, 13, 7, 0xFFFFFFFF);
    root.visible = true;
    Debug.message("SDL3_PIXEL_SMOKE_READY");
} catch(e) {
    Debug.message("SDL3_PIXEL_SMOKE_ERROR: " + (typeof e == "String" ? e : e.message));
}
global.exitTimer = new Timer(function() {
    if(Storages.isExistentStorage(System.exePath + "done.txt")) {
        exitTimer.enabled = false;
        System.exit();
    }
}, "");
exitTimer.interval = 50;
exitTimer.enabled = true;
'''


class XImage(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("xoffset", ctypes.c_int),
        ("format", ctypes.c_int),
        ("data", ctypes.POINTER(ctypes.c_char)),
        ("byte_order", ctypes.c_int),
        ("bitmap_unit", ctypes.c_int),
        ("bitmap_bit_order", ctypes.c_int),
        ("bitmap_pad", ctypes.c_int),
        ("depth", ctypes.c_int),
        ("bytes_per_line", ctypes.c_int),
        ("bits_per_pixel", ctypes.c_int),
        ("red_mask", ctypes.c_ulong),
        ("green_mask", ctypes.c_ulong),
        ("blue_mask", ctypes.c_ulong),
    ]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


XErrorHandlerFunc = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p,
                                    ctypes.c_void_p)


def _swallow_x11_error(display, event):
    # Window enumeration races: a window can vanish between XQueryTree and
    # XGetWindowProperty. Without a handler, Xlib aborts the harness.
    return 0


def collect_backtrace(command, cwd, env, destination):
    """On an engine crash, rerun under gdb and save a backtrace artifact.

    Diagnostic only: does not change pass/fail.
    """
    gdb = shutil.which("gdb")
    if gdb is None:
        return
    # Heap corruption aborts are layout-dependent: retry under gdb a few
    # times so we actually catch one with MALLOC_CHECK_=3 enabled (glibc
    # then aborts at the chunk that gets corrupted, not at a later free).
    bt_path = destination / "gdb-backtrace.log"
    for attempt in range(3):
        try:
            with bt_path.open("wb") as log:
                result = subprocess.run(
                    [gdb, "-batch", "-return-child-result",
                     "-ex", "set environment MALLOC_CHECK_=3",
                     "-ex", "set environment GLIBC_TUNABLES=glibc.malloc.check=3",
                     "-ex", "break exit", "-ex", "break _exit",
                     "-ex", "run", "-ex", "thread apply all bt full",
                     "--args"] + command,
                    cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                    env=env, timeout=120)
            if b"received signal" in bt_path.read_bytes():
                return
        except Exception:
            pass


class X11:
    def __init__(self):
        name = ctypes.util.find_library("X11")
        if not name:
            raise RuntimeError("libX11 is unavailable")
        self.lib = ctypes.CDLL(name)
        self.lib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.lib.XOpenDisplay.restype = ctypes.c_void_p
        self.display = self.lib.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError("Cannot open X display")
        self.lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.lib.XDefaultRootWindow.restype = ctypes.c_ulong
        self.lib.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                        ctypes.POINTER(ctypes.c_ulong),
                                        ctypes.POINTER(ctypes.c_ulong),
                                        ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                                        ctypes.POINTER(ctypes.c_uint)]
        self.lib.XQueryTree.restype = ctypes.c_int
        self.lib.XGetWindowProperty.argtypes = [
            ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_long,
            ctypes.c_long, ctypes.c_int, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))]
        self.lib.XGetWindowProperty.restype = ctypes.c_int
        self.lib.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                          ctypes.POINTER(ctypes.c_ulong),
                                          ctypes.POINTER(ctypes.c_int),
                                          ctypes.POINTER(ctypes.c_int),
                                          ctypes.POINTER(ctypes.c_uint),
                                          ctypes.POINTER(ctypes.c_uint),
                                          ctypes.POINTER(ctypes.c_uint),
                                          ctypes.POINTER(ctypes.c_uint)]
        self.lib.XGetGeometry.restype = ctypes.c_int
        self.lib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                           ctypes.c_uint, ctypes.c_uint]
        self.lib.XResizeWindow.restype = ctypes.c_int
        self.lib.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.lib.XInternAtom.restype = ctypes.c_ulong
        self.lib.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
                                       ctypes.c_int, ctypes.c_uint, ctypes.c_uint,
                                       ctypes.c_ulong, ctypes.c_int]
        self.lib.XGetImage.restype = ctypes.POINTER(XImage)
        self.lib.XGetPixel.argtypes = [ctypes.POINTER(XImage), ctypes.c_int, ctypes.c_int]
        self.lib.XGetPixel.restype = ctypes.c_ulong
        self.lib.XDestroyImage.argtypes = [ctypes.POINTER(XImage)]
        self.lib.XFree.argtypes = [ctypes.c_void_p]
        self.lib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self._error_handler = XErrorHandlerFunc(_swallow_x11_error)
        self.lib.XSetErrorHandler.argtypes = [XErrorHandlerFunc]
        self.lib.XSetErrorHandler(self._error_handler)
        self.root = self.lib.XDefaultRootWindow(self.display)
        self.pid_atom = self.lib.XInternAtom(self.display, b"_NET_WM_PID", 0)
        self.atom_cardinal = self.lib.XInternAtom(self.display, b"CARDINAL", 0)

    def close(self):
        if self.display:
            self.lib.XCloseDisplay(self.display)
            self.display = None

    def children(self, parent):
        root, parent_return = ctypes.c_ulong(), ctypes.c_ulong()
        children, count = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
        if not self.lib.XQueryTree(self.display, parent, ctypes.byref(root),
                                   ctypes.byref(parent_return), ctypes.byref(children),
                                   ctypes.byref(count)):
            return []
        result = [children[i] for i in range(count.value)]
        if children:
            self.lib.XFree(children)
        return result

    def find_window(self, pid):
        stack = [self.root]
        found = []
        while stack:
            parent = stack.pop()
            for child in self.children(parent):
                stack.append(child)
                actual_type, actual_format = ctypes.c_ulong(), ctypes.c_int()
                items, remaining = ctypes.c_ulong(), ctypes.c_ulong()
                data = ctypes.POINTER(ctypes.c_ubyte)()
                status = self.lib.XGetWindowProperty(
                    self.display, child, self.pid_atom, 0, 1, 0,
                    self.atom_cardinal, ctypes.byref(actual_type),
                    ctypes.byref(actual_format), ctypes.byref(items),
                    ctypes.byref(remaining), ctypes.byref(data))
                if status == 0 and data and items.value and actual_format.value == 32:
                    value = ctypes.cast(data, ctypes.POINTER(ctypes.c_ulong))[0]
                    if value == pid:
                        found.append(child)
                if data:
                    self.lib.XFree(data)
        if len(found) != 1:
            raise RuntimeError(f"Expected one X11 window for PID {pid}, found {found}")
        return found[0]

    def resize(self, window, width, height):
        self.lib.XResizeWindow(self.display, window, width, height)
        self.lib.XSync(self.display, 0)

    def geometry(self, window):
        root = ctypes.c_ulong()
        x, y = ctypes.c_int(), ctypes.c_int()
        width, height, border, depth = (ctypes.c_uint() for _ in range(4))
        if not self.lib.XGetGeometry(self.display, window, ctypes.byref(root),
                                     ctypes.byref(x), ctypes.byref(y),
                                     ctypes.byref(width), ctypes.byref(height),
                                     ctypes.byref(border), ctypes.byref(depth)):
            raise RuntimeError("XGetGeometry failed")
        return width.value, height.value

    def capture(self, window, destination):
        width, height = self.geometry(window)
        image = self.lib.XGetImage(self.display, window, 0, 0, width, height,
                                   0xFFFFFFFF, 2)
        if not image:
            raise RuntimeError("XGetImage failed")
        try:
            data = bytearray()
            for y in range(height):
                for x in range(width):
                    pixel = self.lib.XGetPixel(image, x, y)
                    data.extend(((pixel & image.contents.red_mask) >> 16 & 255,
                                 (pixel & image.contents.green_mask) >> 8 & 255,
                                 pixel & image.contents.blue_mask & 255))
            write_ppm(destination, width, height, data)
            return width, height, data
        finally:
            self.lib.XDestroyImage(image)


def write_ppm(path, width, height, rgb):
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + rgb)


def pixel(data, width, x, y):
    start = (y * width + x) * 3
    return tuple(data[start:start + 3])


def check_pixels(width, height, data):
    if (width, height) == (WIDTH, HEIGHT):
        samples = {
            "left_letterbox": (10, 240, COLORS["black"]),
            "right_letterbox": (790, 240, COLORS["black"]),
            "top_left": (160, 120, COLORS["top_left"]),
            "top_right": (640, 120, COLORS["top_right"]),
            "bottom_left": (160, 360, COLORS["bottom_left"]),
            "bottom_right": (640, 360, COLORS["bottom_right"]),
            "asymmetric_marker": (180, 66, 0xFFFFFF),
        }
    elif (width, height) == (1200, 720):
        samples = {
            "left_letterbox": (10, 360, COLORS["black"]),
            "right_letterbox": (1190, 360, COLORS["black"]),
            "top_left": (240, 180, COLORS["top_left"]),
            "top_right": (960, 180, COLORS["top_right"]),
            "bottom_left": (240, 540, COLORS["bottom_left"]),
            "bottom_right": (960, 540, COLORS["bottom_right"]),
            "asymmetric_marker": (275, 99, 0xFFFFFF),
        }
    else:
        raise RuntimeError(f"Unexpected window dimensions: {width}x{height}")
    errors = []
    for name, (x, y, expected) in samples.items():
        actual = pixel(data, width, x, y)
        if actual != ((expected >> 16 & 255), (expected >> 8 & 255), expected & 255):
            errors.append(f"{name}@{x},{y}: {actual} != {expected:06x}")
    return errors


def run(engine, destination, wrong_marker=False):
    engine = engine.resolve(strict=True)
    if destination.exists():
        raise ValueError("Output directory must be new")
    destination.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix="krkr-sdl3-pixel-") as game_tmp:
        game = Path(game_tmp)
        script = FIXTURE.replace("97, 63", "113, 63" if wrong_marker else "97, 63")
        startup = game / "startup.tjs"
        startup.write_text(script, encoding="utf-8")
        report = {"engine": str(engine), "engine_sha256": digest(engine),
                  "script_sha256": digest(startup), "passed": False,
                  "forced_stop": False, "window": None}
        command = [str(engine), "--game=" + str(game)]
        report["command"] = command
        output = destination / "process.log"
        env = os.environ.copy()
        env.setdefault("SDL_VIDEO_DRIVER", "x11")
        with output.open("wb") as log:
            process = subprocess.Popen(command, cwd=engine.parent, stdout=log,
                                       stderr=subprocess.STDOUT, env=env)
            report["pid"] = process.pid
            x11 = None
            try:
                x11 = X11()
                deadline = time.monotonic() + 20
                window = None
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        collect_backtrace(command, engine.parent, env,
                                          destination)
                        raise RuntimeError(
                            f"Engine exited early with {process.returncode}")
                    try:
                        window = x11.find_window(process.pid)
                        if x11.geometry(window) == (WIDTH, HEIGHT):
                            break
                    except RuntimeError:
                        time.sleep(0.1)
                if window is None:
                    raise TimeoutError("SDL3 game window was not mapped")
                if x11.geometry(window) != (WIDTH, HEIGHT):
                    raise TimeoutError("SDL3 game window never reached expected size")
                report["window"] = int(window)
                report["captures"] = []
                for stage, expected_size in (("initial", (WIDTH, HEIGHT)),
                                             ("resized", (1200, 720))):
                    if stage == "resized":
                        x11.resize(window, *expected_size)
                        resize_deadline = time.monotonic() + 5
                        while time.monotonic() < resize_deadline and x11.geometry(window) != expected_size:
                            time.sleep(0.05)
                    deadline = time.monotonic() + 12
                    errors = []
                    while time.monotonic() < deadline:
                        width, height, data = x11.capture(
                            window, destination / f"{stage}.ppm")
                        errors = check_pixels(width, height, data)
                        if not errors:
                            break
                        time.sleep(0.1)
                    report["captures"].append({
                        "stage": stage,
                        "sha256": digest(destination / f"{stage}.ppm"),
                        "errors": errors,
                    })
                    if errors:
                        raise RuntimeError(
                            f"{stage} window pixel mismatch: " + "; ".join(errors))
                report["passed"] = True
            finally:
                if x11 is not None:
                    x11.close()
                if process.poll() is None:
                    game.joinpath("done.txt").write_text("exit\n", encoding="utf-8")
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        report["forced_stop"] = True
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                report["exit_code"] = process.returncode
                report["passed"] = report["passed"] and not report["forced_stop"] and process.returncode == 0
        (destination / "verification.json").write_text(
            json.dumps(report, indent=2), encoding="utf-8")
        if wrong_marker:
            if report["passed"]:
                raise RuntimeError("Negative control unexpectedly passed")
            return False
        if not report["passed"]:
            raise RuntimeError(f"SDL3 pixel smoke failed: {report}")
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--wrong-marker", action="store_true")
    args = parser.parse_args()
    run(args.engine, args.destination, args.wrong_marker)


if __name__ == "__main__":
    main()
