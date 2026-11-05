#!/bin/bash
# Installs Kana-Dojo so it starts automatically instead of the original software.
source "$(dirname "$0")/common.sh"

connect
echo "Software-Version auf dem Gerät: $(firmware_version)"
echo
echo "Nach der Installation startet der reMarkable direkt in Kana-Dojo."
echo "Die Original-Software bleibt auf dem Gerät und lässt sich jederzeit"
echo "zurückholen (Schritt 5, oder in Kana-Dojo unter System)."
read -r -p "Jetzt installieren? [j/N] " answer
case "$answer" in
  j|J|ja|Ja) ;;
  *) echo "Abgebrochen."; exit 0 ;;
esac

upload
say "Richte den Autostart ein …"
rm_ssh /home/root/kana-dojo/bin/kana-dojo-setup install
sleep 4
rm_ssh /home/root/kana-dojo/bin/kana-dojo-setup status
say "Fertig. Viel Spaß beim Üben!"
