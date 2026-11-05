#!/bin/bash
# Copies Kana-Dojo to the tablet and runs it once, without installing.
# Ctrl+C here ends the test and the original software comes back.
source "$(dirname "$0")/common.sh"

connect
upload
say "Starte den Testlauf – schau auf den reMarkable."
echo "Beenden: hier im Terminal Strg+C drücken (oder den reMarkable neu starten)."
rm_ssh_tty /home/root/kana-dojo/bin/kana-dojo-try || true
