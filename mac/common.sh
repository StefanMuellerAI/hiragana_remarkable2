# Shared helpers for the Mac scripts (bash 3.2 compatible).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
RM_HOST="${RM_HOST:-10.11.99.1}"
PAYLOAD="$ROOT/dist/kana-dojo"
RM_KEY="$HOME/.ssh/kana_dojo_remarkable" # created by 0-schluessel-einrichten.sh

# One SSH connection per script run, so the password is asked only once.
SSH_OPTS=(
  -o ControlMaster=auto
  -o ControlPath="/tmp/kana-dojo-ssh-%C"
  -o ControlPersist=180
  -o ConnectTimeout=8
  -o StrictHostKeyChecking=accept-new
  -o UserKnownHostsFile="$ROOT/.known_hosts"
  -o HostKeyAlgorithms=+ssh-rsa
)
if [ -f "$RM_KEY" ]; then
  SSH_OPTS+=(-i "$RM_KEY")
fi

say() { printf '\n\033[1m%s\033[0m\n' "$*"; }
die() { printf '\n\033[31mFehler:\033[0m %s\n' "$*" >&2; exit 1; }

rm_ssh() { ssh "${SSH_OPTS[@]}" "root@$RM_HOST" "$@"; }
rm_ssh_tty() { ssh -t "${SSH_OPTS[@]}" "root@$RM_HOST" "$@"; }

connect() {
  say "Verbinde mit dem reMarkable ($RM_HOST) …"
  if [ ! -f "$RM_KEY" ]; then
    echo "Passwort: auf dem reMarkable unter Einstellungen → Hilfe → Über →"
    echo "Copyrights und Lizenzen (ganz unten, Abschnitt „GPLv3 Compliance“)."
  fi
  if ! rm_ssh true; then
    die "Keine Verbindung. Ist der reMarkable per USB angeschlossen, entsperrt und an?
Falls eine Warnung zum Host-Schlüssel kam (z. B. nach Zurücksetzen): Datei
$ROOT/.known_hosts löschen und nochmal versuchen."
  fi
  local machine
  machine="$(rm_ssh 'cat /sys/devices/soc0/machine' 2>/dev/null || true)"
  case "$machine" in
    *"reMarkable 2"*) ;;
    *) die "Unerwartetes Gerät: '$machine'. Diese Skripte sind nur für das reMarkable 2." ;;
  esac
}

firmware_version() {
  rm_ssh 'grep -h REMARKABLE_RELEASE_VERSION /usr/share/remarkable/update.conf 2>/dev/null | cut -d= -f2' || true
}

# Copies dist/kana-dojo to /home/root/kana-dojo (keeps the learning progress).
upload() {
  [ -x "$PAYLOAD/bin/kana-dojo" ] || die "$PAYLOAD fehlt – bitte das fertige Paket entpacken."
  [ -x "$PAYLOAD/bin/rm2fb_server_swtcon" ] || die "$PAYLOAD/bin/rm2fb_server_swtcon fehlt."
  say "Kopiere Kana-Dojo auf den reMarkable …"
  rm_ssh 'systemctl stop kana-dojo.service kana-dojo-display.service 2>/dev/null; true'
  COPYFILE_DISABLE=1 tar -C "$ROOT/dist" -czf - kana-dojo | rm_ssh 'tar -C /home/root -xzf - && chmod +x /home/root/kana-dojo/bin/*'
  rm_ssh 'du -sh /home/root/kana-dojo | cut -f1' | sed 's/^/Belegt auf dem Gerät: /'
}
