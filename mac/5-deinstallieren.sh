#!/bin/bash
# Removes Kana-Dojo and brings back the original software as autostart.
#   ./5-deinstallieren.sh          removes the program, keeps a copy of your
#                                  learning progress on the Mac
#   ./5-deinstallieren.sh --alles  same, and also deletes the progress on the tablet
source "$(dirname "$0")/common.sh"

connect
if rm_ssh 'test -f /home/root/kana-dojo/data/progress.tsv'; then
  copy="$HOME/reMarkable-Backup/kana-dojo-fortschritt-$(date +%Y-%m-%d_%H%M).tsv"
  mkdir -p "$(dirname "$copy")"
  rm_ssh 'cat /home/root/kana-dojo/data/progress.tsv' > "$copy"
  echo "Lernfortschritt gesichert: $copy"
fi

say "Stelle die Original-Software wieder her …"
if rm_ssh 'test -x /home/root/kana-dojo/bin/kana-dojo-setup'; then
  rm_ssh /home/root/kana-dojo/bin/kana-dojo-setup uninstall
else
  # Program files already gone: remove the units directly.
  rm_ssh 'systemctl disable --now kana-dojo.service kana-dojo-display.service 2>/dev/null
          rm -f /etc/systemd/system/kana-dojo*.service
          systemctl daemon-reload; systemctl enable --now xochitl.service'
fi

if [ "${1:-}" = "--alles" ]; then
  rm_ssh 'rm -rf /home/root/kana-dojo'
  echo "Alle Kana-Dojo-Dateien wurden vom Gerät gelöscht."
else
  rm_ssh 'cd /home/root/kana-dojo && rm -rf bin share'
  echo "Programm entfernt. Dein Lernstand liegt noch unter /home/root/kana-dojo/data."
fi

say "Erledigt – der reMarkable läuft wieder mit der Original-Software."
echo "Für einen komplett sauberen Zustand: auf dem Gerät unter Einstellungen → Allgemein"
echo "die Werkseinstellungen wiederherstellen. Das löscht alle Daten auf dem Gerät."
