# SD-Karte fuer die erste Remote Remote (Stand 2026-09-21)

Ordner-Aufbau wie bei OpenRemote (github.com/LORDSn1per/OpenRemote-Firmware, Ordner `sd-card/`), damit deren
Studio/Konfigurator spaeter mit unserer ESPHome-Firmware zusammenarbeiten kann. Die Karte (FAT32, MBR) wird von
OpenRemote Studio vorbereitet; unsere Dateien kommen dazu, ohne etwas Vorhandenes zu veraendern:

| Pfad | Inhalt | Wer liest es |
|---|---|---|
| `/runtime.json`, `/devices/*.ir`, `/menu.json` | Geraete, Aktivitaeten, Menue (Studio-Export) | runtime_config / menu_ui |
| `/themes/Default/*.rgb565` | Hintergrundbilder 240x320 | menu_ui |
| `/icons/Default`, `/icons/Custom/*.png` | Icons, 64x64 RGBA. `Custom` enthaelt unsere App-Logos | Studio (und spaeter die Firmware) |
| `/covers/<app>_player.rle` | Logo-Cover der Medienseite, 240x320, RGB565 mit Lauflaengen-Kompression ("RL16"), ~5-13 KB | Firmware (`open-remote-sd-bilder.h`) |
| `/covers/<app>_home.rle` | Cover-Banner der Startseite, 216x172, gleiches Format | Firmware |

(Aeltere rohe `.rgb565`-Dateien im selben Ordner werden nur noch gelesen, wenn keine `.rle` da ist; sie duerfen weg.)

`<app>` = Name in Kleinbuchstaben ohne Sonderzeichen, `+` wird zu `plus` (Disney+ -> `disneyplus`).
Erzeugen: `python3 tools/make_sd_assets.py` (Logos von Simple Icons, CC0). Aufspielen: Karte in den PC oder ueber den
Konfig-Modus der Remote (Einstellungen > Wartung > SD-Karte > Konfig-Modus; `POST /sd/api/file?path=...`,
alle 36 Cover in ~30 s, Ordner werden automatisch angelegt).

Nur die erste Remote Firmware bindet die Karte ein. Die SD-Bloecke in `remote-wz-couch.yaml` stehen zwischen
`# >>> NUR-COUCH` und `# <<< NUR-COUCH` (in C++-Lambdas `//`); `tools/sync_wohnzimmer_from_couch.py` laesst sie bei
Remote 2' Datei weg. Karte und Mikrofon teilen sich GPIO15/17/7: beim Einbinden wird der Mikrofon-Treiber gestoppt
(`atv_voice::set_mic_blocked`), nach dem Auswerfen neu gestartet.

**Geschwindigkeit / Mikrofon-Versorgung:** Waehrend die Karte benutzt wird, MUSS MIC_VDD (GPIO45) eingeschaltet bleiben.
Ein stromloses Mikrofon klemmt ueber seine ESD-Diode die gemeinsame Leitung MISO (GPIO7) nach Low; ESP-IDF (`sdspi_host`,
`poll_busy`) wartet vor jedem Befehl bis zu 40 ms, dass MISO hoch geht. Folge: 83 ms je Lesebefehl, ~5 KB/s, egal bei welchem
Takt. Mit Versorgung: ~1 ms je Befehl, 8 MHz -> ~650 KB/s roh, ~215 KB/s ueber FatFS. Nachpruefen: HA-Knopf
"SD: Geschwindigkeitstest" (Log-Tag `sdtest`, erwartet "MISO-Pegel 1" und "Befehl" unter 5000 us).
