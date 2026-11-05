#!/bin/bash
# Saves a complete image of the tablet's internal memory to the Mac.
# Reads only, nothing on the tablet is changed. Takes about 10-20 minutes.
source "$(dirname "$0")/common.sh"

connect
DEST="$HOME/reMarkable-Backup/$(date +%Y-%m-%d_%H%M)"
mkdir -p "$DEST"
say "Backup nach $DEST"

{
  echo "Datum:   $(date)"
  echo "Gerät:   $(rm_ssh 'cat /sys/devices/soc0/machine')"
  echo "Version: $(firmware_version)"
  echo "Seriennr: $(rm_ssh 'cat /sys/devices/soc0/serial_number 2>/dev/null')"
  echo
  rm_ssh 'cat /proc/partitions; echo; mount'
} > "$DEST/geraeteinfo.txt"

free_kb=$(df -k "$DEST" | awk 'NR==2{print $4}')
if [ "$free_kb" -lt 9000000 ]; then
  die "Auf dem Mac sind weniger als 9 GB frei – das Backup braucht bis zu 8 GB."
fi

# Stop the original software so the data doesn't change while copying.
rm_ssh 'systemctl stop xochitl.service kana-dojo.service kana-dojo-display.service 2>/dev/null; true'
restart() {
  rm_ssh 'if systemctl is-enabled kana-dojo.service >/dev/null 2>&1; then systemctl start kana-dojo.service; else systemctl start xochitl.service; fi' || true
}
trap restart EXIT

say "1/3 Bootbereiche (gerätespezifische Daten) …"
for dev in mmcblk2boot0 mmcblk2boot1; do
  rm_ssh "dd if=/dev/$dev bs=1M 2>/dev/null" > "$DEST/$dev.img"
done

say "2/3 Kompletter Speicher (ca. 8 GB) – das dauert 10–20 Minuten …"
dev_bytes=$(( $(rm_ssh 'cat /sys/block/mmcblk2/size') * 512 ))
( rm_ssh 'dd if=/dev/mmcblk2 bs=4M' 2> "$DEST/dd.log" | gzip -1 > "$DEST/mmcblk2.img.gz" ) &
pid=$!
while kill -0 "$pid" 2>/dev/null; do
  sleep 15
  printf '  bisher %s komprimiert\n' "$(du -h "$DEST/mmcblk2.img.gz" | cut -f1)"
done
wait "$pid" || die "Backup abgebrochen (Details: $DEST/dd.log)."
if ! grep -q "$dev_bytes" "$DEST/dd.log"; then
  echo "Warnung: Konnte nicht bestätigen, dass alle $dev_bytes Bytes gelesen wurden:"
  cat "$DEST/dd.log"
fi

say "3/3 Prüfsummen …"
(cd "$DEST" && shasum -a 256 ./*.img ./*.img.gz > SHA256SUMS)

say "Backup fertig:"
ls -lh "$DEST"
echo
echo "Bewahre diesen Ordner gut auf. Normalerweise brauchst du ihn nie –"
echo "zum Zurücksetzen reicht Schritt 5 (Deinstallieren)."
