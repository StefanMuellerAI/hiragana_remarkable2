#!/usr/bin/env python3
"""Runs the real rm2fb_server_swtcon and the real kana-dojo binary (both ARM,
via qemu-arm-static) and checks that they talk to each other. Without
/dev/fb0 the server runs in its built-in emulator mode, so no panel is
needed; RM2FB_DEBUG=1 makes it log every update it receives.

usage: real_server_test.py <rm2fb_server_swtcon> <arm-sysroot> <kana-dojo> <glyphs.bin>
"""
import os, re, socket, subprocess, sys, tempfile, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from fake_rm2fb import Devices, kana_strokes  # noqa: E402

SOCK = "/var/run/rm2fb.sock"


def main():
    server_bin, sysroot, app_bin, glyphs = sys.argv[1:5]
    tmp = tempfile.mkdtemp()
    root = Path(tmp) / "kana-dojo"
    (root / "bin").mkdir(parents=True)
    (root / "share").mkdir()
    subprocess.run(["cp", app_bin, root / "bin/kana-dojo"], check=True)
    subprocess.run(["cp", glyphs, root / "share/glyphs.bin"], check=True)
    dev = Devices(tmp)
    failures = []

    def check(ok, what):
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            failures.append(what)

    slog = open(Path(tmp) / "server.log", "w+")
    server = subprocess.Popen(["qemu-arm-static", "-L", sysroot, server_bin],
                              env=dict(os.environ, RM2FB_DEBUG="1"),
                              stdout=slog, stderr=subprocess.STDOUT)
    for _ in range(100):
        if os.path.exists(SOCK):
            break
        time.sleep(0.1)
    check(os.path.exists(SOCK), "server created its socket")

    probe = socket.socket()
    port_open = probe.connect_ex(("127.0.0.1", 8888)) == 0
    probe.close()
    check(not port_open, "TCP debug port 8888 stays closed (patch)")

    env = dict(os.environ, KANA_DOJO_DRY_RUN="1",
               KANA_DOJO_INPUT_PEN=dev.paths["pen"],
               KANA_DOJO_INPUT_TOUCH=dev.paths["touch"],
               KANA_DOJO_INPUT_POWER=dev.paths["power"])
    app = subprocess.Popen(["qemu-arm-static", str(root / "bin/kana-dojo")], env=env,
                           stderr=subprocess.PIPE, text=True)
    try:
        dev.open_writers()
        time.sleep(4)

        def updates():
            slog.flush()
            return re.findall(r"UPDATE \{ \{\s*(\d+),\s*(\d+);\s*(\d+),\s*(\d+) \}, wave: (\w+) flags: (\w+)",
                              Path(slog.name).read_text(errors="replace"))

        u = updates()
        check(len(u) >= 1, f"server received the app's first frame ({len(u)} updates)")
        check(any(w == "f002" and int(f, 16) & 1 for *_, w, f in u),
              "first frame is GC16 + sync")
        check(any(x2 == "1403" and y2 == "1871" for x1, y1, x2, y2, *_ in u),
              "full-screen update covers 1404x1872")

        dev.tap(700, 1160)  # Lernen
        time.sleep(3)
        n = len(updates())
        for s in kana_strokes("あ"):
            dev.stroke(s)
            time.sleep(1.5)
        u = updates()[n:]
        ink = [x for x in u if x[4] == "f001"]
        check(len(ink) > 10, f"pen strokes arrive as DU ink updates ({len(ink)})")
        check(all(int(x[5], 16) & 2 for x in ink), "ink updates carry the fast-draw flag")
        inside = all(152 <= int(x[0]) and int(x[2]) <= 1252 and 470 <= int(x[1]) and int(x[3]) <= 1570
                     for x in ink)
        check(inside, "ink lands inside the writing box (pen coordinates mapped correctly)")
        check(any(x[4] == "f003" for x in u), "UI changes use GL16")
    finally:
        app.terminate()
        try:
            _, err = app.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            app.kill()
            _, err = app.communicate()
        server.terminate()
        server.wait(timeout=5)
    log = Path(slog.name).read_text(errors="replace")
    check("Got init" in log, "server handled the app's Init message")
    print("--- app stderr ---\n" + (err.strip() or "(empty)")[:800])
    print("--- server log (first lines) ---\n" + "\n".join(log.splitlines()[:12]))
    print("all passed" if not failures else f"{len(failures)} FAILED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
