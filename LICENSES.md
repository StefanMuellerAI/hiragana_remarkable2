# Lizenzen der verwendeten Bestandteile

| Bestandteil | Quelle | Lizenz |
|---|---|---|
| Kana-Dojo (App, Skripte) | dieses Repo | noch keine Lizenz festgelegt |
| Strichdaten `app/src/kana_data.inc` (aus KanjiVG erzeugt) | [KanjiVG](https://kanjivg.tagaini.net/) von Ulrich Apel, Release r20250816 | CC BY-SA 3.0 |
| Schrift für Kana | [Klee One](https://fonts.google.com/specimen/Klee+One) (Fontworks) | SIL Open Font License 1.1 |
| Schrift für Text | [Noto Sans](https://notofonts.github.io/) (Google) | SIL Open Font License 1.1 |
| Bildschirm-Dienst `rm2fb_server_swtcon` | [rM2-stuff](https://github.com/timower/rM2-stuff) von timower, Commit `451577081b9e`, mit den Patches in `patches/` | GPL-3.0 |

Der Bildschirm-Dienst ist als Git-Submodul unter `third_party/rM2-stuff` eingebunden
(Original-Commit); `build.sh` wendet beim Bauen die beiden kleinen Patches aus `patches/` an.
Die Schriften lädt `fetch-deps.sh` samt Lizenztext herunter.
