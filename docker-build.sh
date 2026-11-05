#!/usr/bin/env bash
# Runs a command inside the toltec toolchain image (reMarkable cross compiler).
#   ./docker-build.sh rm2fb <target>...   build rM2-stuff targets
#   ./docker-build.sh sh '<command>'      run a shell command in /src
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
IMAGE=ghcr.io/toltec-dev/base:v4.0

extra=()
# Behind a TLS-intercepting proxy (e.g. a cloud sandbox), pass it along.
if [ -n "${HTTPS_PROXY:-}" ]; then
  extra+=(--network host -e HTTPS_PROXY -e https_proxy -e "http_proxy=$HTTPS_PROXY")
fi
if [ -f /root/.ccr/ca-bundle.crt ]; then
  extra+=(-v /root/.ccr/ca-bundle.crt:/ca.crt:ro -e SSL_CERT_FILE=/ca.crt -e CURL_CA_BUNDLE=/ca.crt)
fi

mode="${1:-}"
shift || true

if [ "$mode" = "sh" ]; then
  exec docker run --rm ${extra[@]+"${extra[@]}"} -v "$ROOT":/src -w /src "$IMAGE" \
    bash -c ". /opt/x-tools/switch-arm.sh; $*"
fi
[ "$mode" = "rm2fb" ] || { echo "usage: $0 rm2fb <target>... | sh '<command>'" >&2; exit 2; }

# Apply our patches to the pinned rM2-stuff checkout (idempotent).
RM="$ROOT/third_party/rM2-stuff"
[ -f "$RM/CMakeLists.txt" ] || { echo "third_party/rM2-stuff fehlt – erst ./fetch-deps.sh ausführen" >&2; exit 1; }
for p in "$ROOT"/patches/*.patch; do
  if git -C "$RM" apply --reverse --check "$p" 2>/dev/null; then
    continue
  fi
  echo "Patch: $(basename "$p")"
  git -C "$RM" apply "$p"
done

docker run --rm ${extra[@]+"${extra[@]}"} \
  -v "$ROOT":/src -w /src/third_party/rM2-stuff "$IMAGE" bash -c '
    set -e
    command -v xxd >/dev/null || export PATH="/src/tools/shims:$PATH"
    . /opt/x-tools/switch-arm.sh
    D=/src/third_party/deps
    [ -f build/release-toltec/build.ninja ] || cmake --preset release-toltec \
      -DFETCHCONTENT_SOURCE_DIR_EXPECTED=$D/expected \
      -DFETCHCONTENT_SOURCE_DIR_UTFCPP=$D/utfcpp \
      -DFETCHCONTENT_SOURCE_DIR_FRIDA-GUM=$D/frida-gum >/dev/null
    cmake --build build/release-toltec --target "$@"
    "${CROSS_COMPILE:-arm-remarkable-linux-gnueabihf-}strip" \
      build/release-toltec/libs/rm2fb/rm2fb_server_swtcon 2>/dev/null || true
  ' bash "$@"
