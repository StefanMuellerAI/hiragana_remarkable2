#!/usr/bin/env bash
# Builds the device package into dist/kana-dojo.
#   ./build.sh            app + display server
#   ./build.sh app        only the Kana-Dojo app (quick)
#   ./build.sh package    everything, plus dist/kana-dojo-rm2.zip for the Mac
# Needs: Docker, python3 with Pillow. Run ./fetch-deps.sh once before.
# Set KANJIVG_DIR=<kanjivg>/kanji to regenerate the stroke data.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/dist/kana-dojo"
mode="${1:-all}"
mkdir -p "$OUT/bin" "$OUT/share/systemd" "$OUT/share/licenses"

if [ -n "${KANJIVG_DIR:-}" ]; then
  python3 "$ROOT/tools/gen_kana_data.py" "$KANJIVG_DIR" "$ROOT/app/src/kana_data.inc"
fi
if [ ! -f "$ROOT/app/assets/glyphs.bin" ]; then
  [ -f "$ROOT/app/assets/fonts/KleeOne-SemiBold.ttf" ] || { echo "Schriften fehlen – erst ./fetch-deps.sh ausführen" >&2; exit 1; }
  python3 "$ROOT/tools/gen_glyphs.py" "$ROOT/app/assets/glyphs.bin"
fi

echo "== Kana-Dojo (statisch gelinkt, ARMv7)"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-psabi -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard -static -s"
SRC="app/src/main_rm2.cpp app/src/app.cpp app/src/gfx.cpp app/src/checker.cpp app/src/kana.cpp app/src/progress.cpp"
if command -v arm-linux-gnueabihf-g++ >/dev/null; then
  (cd "$ROOT" && arm-linux-gnueabihf-g++ $CXXFLAGS -o dist/kana-dojo/bin/kana-dojo $SRC)
else
  "$ROOT/docker-build.sh" sh "arm-linux-gnueabihf-g++ $CXXFLAGS -o dist/kana-dojo/bin/kana-dojo $SRC"
fi

if [ "$mode" != "app" ]; then
  echo "== Bildschirm-Dienst (rM2-stuff rm2fb, swtcon-Modus)"
  "$ROOT/docker-build.sh" rm2fb rm2fb_server_swtcon
  cp "$ROOT/third_party/rM2-stuff/build/release-toltec/libs/rm2fb/rm2fb_server_swtcon" "$OUT/bin/"
fi

cp "$ROOT/app/assets/glyphs.bin" "$OUT/share/"
cp "$ROOT"/device/*.service "$OUT/share/systemd/"
cp "$ROOT/device/kana-dojo-setup" "$ROOT/device/kana-dojo-try" "$OUT/bin/"
cp "$ROOT/LICENSES.md" "$OUT/share/licenses/"
cp "$ROOT"/app/assets/fonts/*-OFL.txt "$OUT/share/licenses/" 2>/dev/null || true
[ -f "$ROOT/third_party/rM2-stuff/LICENSE" ] &&
  cp "$ROOT/third_party/rM2-stuff/LICENSE" "$OUT/share/licenses/rM2-stuff-GPL-3.0.txt"
chmod +x "$OUT"/bin/*
echo "== Paket: $OUT"
ls -l "$OUT/bin"

if [ "$mode" = "package" ]; then
  [ -x "$OUT/bin/rm2fb_server_swtcon" ] || { echo "rm2fb_server_swtcon fehlt" >&2; exit 1; }
  stage="$(mktemp -d)"
  mkdir -p "$stage/kana-dojo/dist"
  cp -a "$ROOT/README.md" "$ROOT/LICENSES.md" "$ROOT/mac" "$stage/kana-dojo/"
  cp -a "$OUT" "$stage/kana-dojo/dist/"
  rm -f "$ROOT/dist/kana-dojo-rm2.zip"
  (cd "$stage" && python3 -c "
import os, zipfile, sys
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    for base, dirs, files in os.walk('kana-dojo'):
        for f in sorted(files):
            p = os.path.join(base, f)
            info = zipfile.ZipInfo.from_file(p)
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(p, 'rb') as fh:
                z.writestr(info, fh.read())
" "$ROOT/dist/kana-dojo-rm2.zip")
  rm -rf "$stage"
  echo "== Mac-Paket: $ROOT/dist/kana-dojo-rm2.zip"
fi
