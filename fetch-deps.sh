#!/usr/bin/env bash
# Downloads everything the build needs that isn't stored in this repo:
# fonts for the UI, the rM2-stuff submodule and its build dependencies.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
D="$ROOT/third_party/deps"
F="$ROOT/app/assets/fonts"
mkdir -p "$D" "$F"

fetch() { # url dest
  [ -s "$2" ] || { echo "  $2"; curl -fsSL -o "$2" "$1"; }
}

echo "== Schriften (SIL Open Font License)"
GF=https://raw.githubusercontent.com/google/fonts/main/ofl
NOTO=https://raw.githubusercontent.com/notofonts/notofonts.github.io/main/fonts/NotoSans/hinted/ttf
fetch "$GF/kleeone/KleeOne-SemiBold.ttf" "$F/KleeOne-SemiBold.ttf"
fetch "$GF/kleeone/OFL.txt" "$F/KleeOne-OFL.txt"
fetch "$NOTO/NotoSans-Regular.ttf" "$F/NotoSans-Regular.ttf"
fetch "$NOTO/NotoSans-Bold.ttf" "$F/NotoSans-Bold.ttf"
fetch "$GF/notosans/OFL.txt" "$F/NotoSans-OFL.txt"

echo "== rM2-stuff (Submodul)"
# The repo keeps a few binary assets in Git LFS that the display server
# doesn't need, so skip them.
GIT_LFS_SKIP_SMUDGE=1 git -C "$ROOT" submodule update --init third_party/rM2-stuff

echo "== Abhängigkeiten von rM2-stuff"
[ -d "$D/expected" ] || git clone -q --depth 1 --branch v1.1.0 https://github.com/TartanLlama/expected.git "$D/expected"
[ -d "$D/utfcpp" ] || git clone -q --depth 1 --branch v4.0.0 https://github.com/nemtrif/utfcpp.git "$D/utfcpp"
if [ ! -d "$D/frida-gum" ]; then
  mkdir -p "$D/frida-gum"
  curl -fsSL https://github.com/frida/frida/releases/download/16.1.4/frida-gum-devkit-16.1.4-linux-armhf.tar.xz |
    tar -xJ -C "$D/frida-gum"
fi
echo "Fertig."
