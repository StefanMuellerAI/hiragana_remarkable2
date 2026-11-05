#!/usr/bin/env python3
"""Integration test: runs the real ARM binary (via qemu-arm-static) against a
fake rm2fb server that speaks the same socket protocol, and feeds it
synthetic evdev events. Saves framebuffer snapshots as PNG."""
import array, mmap, os, socket, struct, subprocess, sys, tempfile, threading, time
from pathlib import Path

W, H = 1404, 1872
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


class FakeServer(threading.Thread):
    def __init__(self, path):
        super().__init__(daemon=True)
        self.path = path
        self.updates = []
        self.inputs = []
        self.mem = None
        self.ready = threading.Event()
        self.lock = threading.Lock()
        if os.path.exists(path):
            os.unlink(path)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.bind(path)
        self.sock.listen(1)

    def recv_exact(self, c, n):
        buf = b""
        while len(buf) < n:
            chunk = c.recv(n - len(buf))
            if not chunk:
                raise EOFError
            buf += chunk
        return buf

    def send_fd(self, c, fd):
        c.sendmsg([b"\0"], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [fd]))])

    def run(self):
        c, _ = self.sock.accept()
        try:
            while True:
                idx, = struct.unpack("<i", self.recv_exact(c, 4))
                if idx == 0:  # Init
                    own, w, h, fmt = struct.unpack("<?3xiii", self.recv_exact(c, 16))
                    assert (own, w, h, fmt) == (False, W, H, 0), (own, w, h, fmt)
                    fd = os.memfd_create("swtfb")
                    os.ftruncate(fd, W * H * 3)
                    self.mem = mmap.mmap(fd, W * H * 3)
                    self.mem[: W * H * 2] = b"\xff" * (W * H * 2)
                    c.sendall(struct.pack("<iii", W, H, 0))
                    self.send_fd(c, fd)
                    self.ready.set()
                elif idx == 2:  # UpdateParams
                    y1, x1, y2, x2, flags, wave, temp, extra = struct.unpack(
                        "<iiiiiifi", self.recv_exact(c, 32))
                    assert wave & 0xF000, "waveform flag missing"
                    with self.lock:
                        self.updates.append((wave & 0xFFF, flags, x1, y1, x2, y2))
                    c.sendall(b"\x01")
                elif idx == 4:  # OpenInputDevice
                    raw = self.recv_exact(c, 68)
                    path = raw[:64].split(b"\0")[0].decode()
                    flags, = struct.unpack("<i", raw[64:])
                    self.inputs.append(path)
                    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
                    c.sendall(b"\x01")
                    self.send_fd(c, fd)
                else:
                    raise AssertionError(f"unknown message {idx}")
        except EOFError:
            pass

    def snapshot(self, out):
        from PIL import Image
        px = array.array("H", self.mem[: W * H * 2])
        grey = bytes(((v >> 5) & 0x3F) << 2 for v in px)
        Image.frombytes("L", (W, H), grey).save(out)


def ev(t, code, value):
    return struct.pack("<IIHHi", 0, 0, t, code, value)


EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
SYN = ev(EV_SYN, 0, 0)


class Devices:
    def __init__(self, d):
        self.paths = {}
        self.writers = {}
        for name in ("pen", "touch", "power"):
            p = os.path.join(d, name)
            os.mkfifo(p)
            self.paths[name] = p

    def open_writers(self):
        for name, p in self.paths.items():
            self.writers[name] = os.open(p, os.O_WRONLY)

    def write(self, name, data):
        os.write(self.writers[name], data)

    def tap(self, x, y):
        self.write("touch", ev(EV_ABS, 0x2F, 0) + ev(EV_ABS, 0x39, 7) +
                   ev(EV_ABS, 0x35, x) + ev(EV_ABS, 0x36, H - y) + SYN)
        time.sleep(0.05)
        self.write("touch", ev(EV_ABS, 0x39, -1) + SYN)

    def stroke(self, pts):
        def raw(x, y):
            return int((H - y) * 20967 / H), int(x * 15725 / W)
        self.write("pen", ev(EV_KEY, 0x140, 1) + SYN)
        for i, (x, y) in enumerate(pts):
            rx, ry = raw(x, y)
            data = ev(EV_ABS, 0, rx) + ev(EV_ABS, 1, ry)
            if i == 0:
                data += ev(EV_KEY, 0x14A, 1)
            self.write("pen", data + SYN)
            time.sleep(0.01)  # real pens report at a few hundred Hz
        self.write("pen", ev(EV_KEY, 0x14A, 0) + SYN + ev(EV_KEY, 0x140, 0) + SYN)

    def power(self):
        self.write("power", ev(EV_KEY, 116, 1) + SYN + ev(EV_KEY, 116, 0) + SYN)


def kana_strokes(glyph):
    import re
    src = (ROOT / "app/src/kana_data.inc").read_text(encoding="utf-8")
    pts = [(float(a), float(b)) for a, b in re.findall(r"\{([\d.]+)f,([\d.]+)f\}",
           src[src.index("kKanaPoints"):src.index("kKanaStrokes")])]
    strokes = [(int(a), int(b)) for a, b, _, _ in re.findall(
        r"\{(\d+), (\d+), ([\d.]+)f, ([\d.]+)f\}", src[src.index("kKanaStrokes[]"):src.index("kKanaDefs")])]
    for g, r, s, grp, f, c in re.findall(r'\{"([^"]+)", "([^"]+)", ([01]), (\d+), (\d+), (\d+)\}', src):
        if g == glyph:
            out = []
            for k in range(int(f), int(f) + int(c)):
                a, n = strokes[k]
                out.append([(152 + x * 1100 / 109, 470 + y * 1100 / 109) for x, y in pts[a:a + n]])
            return out
    raise KeyError(glyph)


def main():
    binary, glyphs, outdir = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    outdir.mkdir(parents=True, exist_ok=True)
    tmp = tempfile.mkdtemp()
    root = Path(tmp) / "kana-dojo"
    (root / "bin").mkdir(parents=True)
    (root / "share").mkdir()
    os.link(binary, root / "bin/kana-dojo") if False else subprocess.run(["cp", binary, root / "bin/kana-dojo"], check=True)
    subprocess.run(["cp", glyphs, root / "share/glyphs.bin"], check=True)

    server = FakeServer(os.path.join(tmp, "rm2fb.sock"))
    server.start()
    dev = Devices(tmp)
    shot = os.path.join(tmp, "screen.bmp")
    env = dict(os.environ, KANA_DOJO_SOCKET=server.path, KANA_DOJO_DRY_RUN="1",
               KANA_DOJO_SCREENSHOT=shot,
               KANA_DOJO_INPUT_PEN=dev.paths["pen"], KANA_DOJO_INPUT_TOUCH=dev.paths["touch"],
               KANA_DOJO_INPUT_POWER=dev.paths["power"])
    proc = subprocess.Popen(["qemu-arm-static", str(root / "bin/kana-dojo")], env=env,
                            stderr=subprocess.PIPE, text=True)
    failures = []

    def check(ok, what):
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            failures.append(what)

    def settle(timeout=8.0):
        last, t0 = -1, time.time()
        while time.time() - t0 < timeout:
            time.sleep(0.4)
            with server.lock:
                n = len(server.updates)
            if n == last:
                return
            last = n

    try:
        check(server.ready.wait(20), "client sent Init and mapped the framebuffer")
        dev.open_writers()
        settle()
        check(len(server.inputs) == 3, f"opened 3 input devices via server ({len(server.inputs)})")
        check(any(u[0] == 2 and u[1] & 1 for u in server.updates), "first frame is a full GC16 sync update")
        server.snapshot(outdir / "it-01-home.png")

        import signal
        from PIL import Image, ImageChops
        os.kill(proc.pid, signal.SIGUSR1)
        for _ in range(50):
            if os.path.exists(shot):
                break
            time.sleep(0.1)
        ok = os.path.exists(shot)
        if ok:
            bmp = Image.open(shot).convert("L")
            fb = Image.open(outdir / "it-01-home.png").convert("L")
            # The panel buffer is RGB565, so allow the 6-bit grey rounding.
            diff = ImageChops.difference(bmp, fb).getextrema()[1]
            ok = bmp.size == (W, H) and diff <= 4
        check(ok, "SIGUSR1 writes a screenshot matching the screen")

        dev.tap(700, 1160)  # Lernen
        settle()
        server.snapshot(outdir / "it-02-learn.png")
        n_before = len(server.updates)
        strokes = kana_strokes("あ")
        dev.stroke(strokes[0])
        settle()
        ink = [u for u in server.updates[n_before:] if u[0] == 1]
        check(len(ink) > 3 and all(u[1] & 2 for u in ink), f"pen ink uses fast DU updates ({len(ink)})")
        for s in strokes[1:]:
            dev.stroke(s)
            settle()
        server.snapshot(outdir / "it-03-a-written.png")
        # Centre pixel of the writing box region where stroke 2 crosses should be black.
        px = array.array("H", server.mem[: W * H * 2])
        x, y = [int(v) for v in strokes[1][len(strokes[1]) // 2]]
        check(px[y * W + x] == 0, "stroke pixels reached the shared framebuffer")
        time.sleep(2.0)  # auto-advance after a clean kana
        settle()
        server.snapshot(outdir / "it-04-next.png")

        dev.power()
        time.sleep(1.0)
        settle()
    finally:
        proc.terminate()
        try:
            _, err = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            _, err = proc.communicate()
    check("[dry-run] suspend" in err, "power key triggers suspend")
    print("--- app stderr ---\n" + err.strip()[:1500])
    print(f"{len(server.updates)} updates")
    print("all passed" if not failures else f"{len(failures)} FAILED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
