#!/bin/bash
# Collects everything needed to see what Kana-Dojo is doing on the tablet:
# service status, recent log lines (one per written stroke) and a screenshot.
# Changes nothing on the tablet.
source "$(dirname "$0")/common.sh"

connect
DEST="$HOME/reMarkable-Backup/diagnose-$(date +%Y-%m-%d_%H%M%S)"
mkdir -p "$DEST"

say "Status"
if rm_ssh 'test -x /home/root/kana-dojo/bin/kana-dojo-setup'; then
  rm_ssh /home/root/kana-dojo/bin/kana-dojo-setup status | tee "$DEST/status.txt"
else
  echo "Kana-Dojo ist nicht auf dem Gerät." | tee "$DEST/status.txt"
fi

say "Protokoll (letzte Zeilen)"
rm_ssh 'if systemctl is-enabled kana-dojo.service >/dev/null 2>&1; then
          journalctl -u kana-dojo.service -u kana-dojo-display.service -n 60 --no-pager
        elif [ -f /tmp/kana-dojo-try.log ]; then
          tail -n 60 /tmp/kana-dojo-try.log
        else
          echo "(kein Protokoll gefunden)"
        fi' | tee "$DEST/protokoll.txt"

say "Screenshot"
if rm_ssh 'pid=$(pidof kana-dojo) && rm -f /tmp/kana-dojo-screen.bmp && kill -USR1 $pid &&
           for i in 1 2 3 4 5 6 7 8 9 10; do [ -f /tmp/kana-dojo-screen.bmp ] && exit 0; sleep 0.5; done; exit 1'; then
  rm_ssh 'cat /tmp/kana-dojo-screen.bmp' > "$DEST/bildschirm.bmp"
  echo "Gespeichert: $DEST/bildschirm.bmp"
  command -v open >/dev/null && open "$DEST/bildschirm.bmp"
else
  echo "Kein Screenshot – Kana-Dojo läuft gerade nicht."
fi

echo
echo "Alles liegt in: $DEST"
