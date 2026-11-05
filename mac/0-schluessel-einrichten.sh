#!/bin/bash
# Optional: sets up an SSH key so the other scripts (and Claude, if you let
# it work on your Mac) can reach the tablet without typing the password.
# Asks for the password one last time. Undo: delete /home/root/.ssh/authorized_keys
# on the tablet (a factory reset does that too).
source "$(dirname "$0")/common.sh"

if [ ! -f "$RM_KEY" ]; then
  say "Erzeuge einen eigenen Schlüssel nur für den reMarkable: $RM_KEY"
  mkdir -p "$HOME/.ssh"
  ssh-keygen -q -t ed25519 -N "" -C "kana-dojo@$(hostname -s)" -f "$RM_KEY"
fi

connect
say "Hinterlege den Schlüssel auf dem reMarkable …"
rm_ssh 'mkdir -p /home/root/.ssh && chmod 700 /home/root/.ssh && touch /home/root/.ssh/authorized_keys &&
        chmod 600 /home/root/.ssh/authorized_keys'
key="$(cat "$RM_KEY.pub")"
if rm_ssh "grep -qF '$key' /home/root/.ssh/authorized_keys"; then
  echo "Der Schlüssel war schon hinterlegt."
else
  echo "$key" | rm_ssh 'cat >> /home/root/.ssh/authorized_keys'
fi

# Check with a fresh connection that only uses the key.
ssh -o ControlPath=none -o BatchMode=yes -o IdentitiesOnly=yes -i "$RM_KEY" \
  -o UserKnownHostsFile="$ROOT/.known_hosts" -o HostKeyAlgorithms=+ssh-rsa \
  -o ConnectTimeout=8 "root@$RM_HOST" true \
  && say "Fertig – ab jetzt geht es ohne Passwort." \
  || die "Der Schlüssel wird nicht akzeptiert. Die Skripte funktionieren weiter mit Passwort."
