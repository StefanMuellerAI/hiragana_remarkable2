# かな道場 Kana-Dojo – Hiragana & Katakana schreiben auf dem reMarkable 2

Kana-Dojo verwandelt ein reMarkable 2 in einen Schreibtrainer für japanische Silbenschrift.
Du schreibst mit dem Stift, und die App prüft **jeden einzelnen Strich** sofort:
Richtung, Reihenfolge und Form. Bei einem Fehler zeigt sie dir den richtigen Strich mit Pfeil.

- **Lernen** – Zeichen mit grauer Vorlage nachfahren, mit Strichnummern und Strichfolge-Leiste.
- **Üben** – aus dem Gedächtnis schreiben (nur die Lautschrift wird angezeigt), mit Korrektur und „Tipp“-Knopf.
- **Test** – Lernkartei mit 6 Stufen: fällige Zeichen werden wiederholt, Fehler schicken ein Zeichen zurück auf Stufe 1.
- **Fortschritt** – Übersicht aller 151 Zeichen (Hiragana, Katakana, mit Dakuten, kleine Kana, ー) mit Lernstufe.
- Einschaltknopf = Ruhezustand, nach 10 Minuten ohne Eingabe schläft das Gerät automatisch.

## Was auf dem Gerät verändert wird – und was nicht

- Die App liegt komplett in **`/home/root/kana-dojo`** (ca. 10 MB).
- Für den Autostart kommen **drei kleine Dienst-Dateien** nach `/etc/systemd/system/`
  (`kana-dojo.service`, `kana-dojo-display.service`, `kana-dojo-fallback.service`),
  und der Autostart der Original-Software wird abgeschaltet (nicht gelöscht).
- **Nicht angefasst** werden: Kernel, Bootloader, die Original-Software selbst, deine Dokumente.
- **Sicherheitsnetz:** Startet Kana-Dojo fünfmal hintereinander nicht, startet automatisch die Original-Software.
- Ein Firmware-Update der Original-Software ersetzt das Systemlaufwerk – danach startet wieder die Original-Software
  (Kana-Dojo müsste dann neu installiert werden).

## Schritt für Schritt (am Mac)

Du brauchst: das reMarkable 2 per USB-Kabel am Mac, eingeschaltet und entsperrt.

**0. Vorbereiten.** Paket entpacken, das Programm *Terminal* öffnen und in den Ordner wechseln:

```bash
cd ~/Downloads/kana-dojo
```

Das **SSH-Passwort** steht auf dem reMarkable unter
*Einstellungen → Hilfe → Über → Copyrights und Lizenzen*, ganz unten im Abschnitt „GPLv3 Compliance“.
Jedes Skript fragt einmal danach. (Je nach Software-Version kann der Menüpfad leicht anders heißen.)

**Optional: SSH-Schlüssel einrichten** – danach fragt kein Skript mehr nach dem Passwort
(und Claude kann auf deinem Mac ohne Passwort-Eingabe mit dem Gerät arbeiten):

```bash
bash mac/0-schluessel-einrichten.sh
```

**1. Verbindung prüfen** – ändert nichts, zeigt Gerät und Software-Version:

```bash
bash mac/1-verbindung-pruefen.sh
```

**2. Backup** – sichert den kompletten internen Speicher (ca. 8 GB, 10–20 Minuten) nach `~/reMarkable-Backup/`:

```bash
bash mac/2-backup.sh
```

**3. Testlauf** – startet Kana-Dojo einmal, ohne etwas zu installieren. Mit `Strg+C` im Terminal
(oder einem Neustart des reMarkable) ist sofort wieder die Original-Software da:

```bash
bash mac/3-testlauf.sh
```

**4. Installieren** – ab jetzt startet der reMarkable direkt in Kana-Dojo:

```bash
bash mac/4-installieren.sh
```

**Bei Problemen: Diagnose** – zeigt Status und Protokoll (eine Zeile pro geschriebenem Strich)
und holt einen Screenshot vom Gerät auf den Mac. Ändert nichts:

```bash
bash mac/6-diagnose.sh
```

## Installation zusammen mit Claude

Claude kann die Schritte oben auch selbst ausführen und dabei mittesten (Protokoll und Screenshots
lesen, während du auf dem Gerät schreibst). Dafür braucht Claude Zugriff auf deinen Mac, weil der
reMarkable dort per USB hängt:

- **Ordner freigeben:** in der Claude-Desktop-App unter *Einstellungen → Claude Code → Remote Control*
  den Ordner mit diesem Repo freigeben. Eine Cloud-Sitzung kann ihn dann nutzen.
  (Nicht für alle Konten schon verfügbar.)
- **Oder lokal arbeiten:** eine Claude-Code-Sitzung direkt auf dem Mac in diesem Ordner starten
  (Claude-Desktop-App oder `claude` im Terminal).

Vorher einmal `bash mac/0-schluessel-einrichten.sh` ausführen, damit Claude kein Passwort braucht.

## Zurück zur Original-Software

| Was | Wie | Ergebnis |
|---|---|---|
| Kurz zurück | In Kana-Dojo: *System → Original reMarkable-Software starten* | Original läuft bis zum nächsten Neustart |
| Dauerhaft zurück | `bash mac/5-deinstallieren.sh` | Autostart wie vorher, Programm entfernt, Lernstand wird auf den Mac kopiert |
| Alles weg | `bash mac/5-deinstallieren.sh --alles` | zusätzlich alle Kana-Dojo-Dateien gelöscht |
| Wie neu | danach auf dem Gerät die Werkseinstellungen wiederherstellen (*Einstellungen → Allgemein*, Menüpfad je nach Version leicht anders) | Werkszustand (löscht alle Daten) |

**Notfall:** Der SSH-Zugang über USB läuft unabhängig von der Oberfläche. Selbst wenn der Bildschirm
hängen sollte, funktioniert `bash mac/5-deinstallieren.sh` weiterhin. Das Backup aus Schritt 2 ist die
letzte Absicherung; zurückspielen lässt es sich nur über den USB-Wiederherstellungsmodus
(Anleitungen der Community, z. B. *remarkable2-recovery*) oder mit Hilfe des reMarkable-Supports.

## Wenn etwas nicht klappt

- **„Keine Verbindung“**: Kabel prüfen, Gerät entsperren. Im Browser sollte `http://10.11.99.1` erreichbar sein,
  wenn unter *Einstellungen → Speicher* das USB-Webinterface eingeschaltet ist.
- **Warnung zum Host-Schlüssel** (z. B. nach Zurücksetzen): Datei `.known_hosts` im Paketordner löschen.
- **Testlauf zeigt nichts / Bildschirm-Dienst beendet sich**: Die Ausgabe im Terminal und das Protokoll
  `/tmp/kana-dojo-try.log` auf dem Gerät zeigen den Grund. Laut seinem Entwickler ist der Bildschirm-Dienst
  mit den Software-Versionen 3.20 bis 3.28 getestet; ältere oder neuere Versionen können abweichen.
- **Stift-Eingabe wirkt versetzt oder gespiegelt**: Bitte melden – die Umrechnung ist ohne echtes Gerät nur simuliert getestet.

## Für Entwickler

Das fertige Mac-Paket (`kana-dojo-rm2.zip`) entsteht aus diesem Repo so (braucht Docker und Python 3 mit Pillow):

```bash
git clone --recursive https://github.com/StefanMuellerAI/hiragana_remarkable2.git
cd hiragana_remarkable2
./fetch-deps.sh
./build.sh package
```

Das Ergebnis liegt in `dist/kana-dojo-rm2.zip` (und entpackt in `dist/kana-dojo/`).

```
app/src/        App (C++17, ohne Abhängigkeiten): Prüflogik, Oberfläche, Lernkartei, Geräteanbindung
app/src/kana_data.inc   Strichdaten aller Kana, erzeugt aus KanjiVG (tools/gen_kana_data.py)
app/tests/      checker_test.cpp   Prüflogik mit simulierten Handschriften
                sim.cpp            UI-Simulator, schreibt Screenshots
                fake_rm2fb.py      ARM-App gegen nachgebauten Bildschirm-Dienst (qemu)
                real_server_test.py ARM-App gegen den echten Bildschirm-Dienst (qemu)
tools/          Generatoren für Strichdaten und Schriften, xxd-Ersatz für den Build
device/         Dienst-Dateien und Skripte, die auf dem Gerät landen
mac/            Skripte für den Mac (Schlüssel, Verbindung, Backup, Testlauf, Installation,
                Deinstallation, Diagnose)
patches/        eigene Änderungen am Bildschirm-Dienst
third_party/rM2-stuff   Bildschirm-Dienst (Git-Submodul, Commit 451577081b9e)
```

Tests (Linux, normales `g++`; die beiden Integrationstests brauchen zusätzlich `qemu-arm-static`):

```bash
g++ -std=c++17 -O2 -o /tmp/checker_test app/tests/checker_test.cpp app/src/checker.cpp app/src/kana.cpp && /tmp/checker_test
```

```bash
g++ -std=c++17 -O2 -o /tmp/sim app/tests/sim.cpp app/src/{app,gfx,checker,kana,progress}.cpp && mkdir -p /tmp/shots && /tmp/sim /tmp/shots app/assets/glyphs.bin
```

```bash
python3 app/tests/fake_rm2fb.py dist/kana-dojo/bin/kana-dojo app/assets/glyphs.bin /tmp/it
```

Für `real_server_test.py` wird zusätzlich das ARM-Sysroot der toltec-Toolchain gebraucht
(`docker run --rm -v /tmp/armroot:/out ghcr.io/toltec-dev/base:v4.0 cp -a /opt/x-tools/arm-remarkable-linux-gnueabihf/arm-remarkable-linux-gnueabihf/sysroot/. /out/`):

```bash
python3 app/tests/real_server_test.py dist/kana-dojo/bin/rm2fb_server_swtcon /tmp/armroot dist/kana-dojo/bin/kana-dojo app/assets/glyphs.bin
```

Lizenzen: siehe [LICENSES.md](LICENSES.md).
