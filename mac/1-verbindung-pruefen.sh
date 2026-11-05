#!/bin/bash
# Checks the USB connection and shows what's on the tablet. Changes nothing.
source "$(dirname "$0")/common.sh"

connect
say "Verbindung steht."
echo "Gerät:            $(rm_ssh 'cat /sys/devices/soc0/machine')"
echo "Software-Version: $(firmware_version)"
echo "Freier Speicher:  $(rm_ssh "df -h /home | awk 'NR==2{print \$4}'")"
echo "Akku:             $(rm_ssh 'cat /sys/class/power_supply/max77818_battery/capacity 2>/dev/null')%"
if rm_ssh 'test -x /home/root/kana-dojo/bin/kana-dojo-setup'; then
  echo
  rm_ssh '/home/root/kana-dojo/bin/kana-dojo-setup status'
fi

glibc="$(rm_ssh '/lib/libc.so.6 2>&1 | head -n1' | sed -n 's/.*version \([0-9.]*\).*/\1/p')"
echo "glibc:            ${glibc:-unbekannt}"
case "$glibc" in
  2.3[4-9]*|2.[4-9]*|3.*) ;;
  "") echo "Hinweis: glibc-Version nicht erkannt." ;;
  *) echo; echo "Achtung: Der Bildschirm-Dienst braucht glibc 2.34 oder neuer – bitte zuerst die"
     echo "reMarkable-Software aktualisieren." ;;
esac

version="$(firmware_version)"
case "$version" in
  3.2[0-8].*) echo; echo "Diese Version ist mit dem Bildschirm-Dienst getestet (3.20–3.28)." ;;
  *) echo; echo "Hinweis: Version $version ist nicht ausdrücklich getestet (getestet: 3.20–3.28)."
     echo "Der Testlauf (Schritt 3) zeigt gefahrlos, ob es trotzdem funktioniert." ;;
esac
