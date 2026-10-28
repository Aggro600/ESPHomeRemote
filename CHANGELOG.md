# What's new · Was ist neu

## 2026.10.28.3 (2026-10-28)

Fixed update error "menue: expected a dictionary": the firmware package now loads its components straight from GitHub instead of the ESPHome folder – old copies there no longer interfere. Under Firmware the configurator detects old files in the ESPHome folder (components, esphomeremote-*.h, fonts) and moves them to the archive with one click. Remote: when the battery is full on the charger, the flickering charge signal no longer keeps the display on (it could stay lit for hours).

<details><summary>Deutsch</summary>

Update-Fehler „menue: expected a dictionary“ behoben: Das Firmware-Paket holt seine Komponenten jetzt direkt von GitHub statt aus dem ESPHome-Ordner – alte Kopien dort stören nicht mehr. Der Konfigurator erkennt unter Firmware alte Dateien im ESPHome-Ordner (Komponenten, esphomeremote-*.h, Schriften) und räumt sie auf Knopfdruck ins Archiv. Remote: ist der Akku am Kabel voll, flackert die Lade-Meldung nicht mehr das Display an (Display blieb sonst stundenlang an).

</details>

## 2026.10.28.2 (2026-10-28)

- Fresh start: the repository starts over without the old version history (cleaned up)
- Dock: search pause while the TV/streamer reconnects is active again (works around a crash in the ESP32-C3 Bluetooth controller)
- Configurator, docking station: new section "known bugs" – checks GitHub daily whether Espressif has fixed the bug and lets you know when it's worth trying again
- Menu and home screen editor: an entry's options in collapsible groups with a short summary
- A remote's infrared dialog shows all IR databases and your own codes
- Page option "volume up / down / mute control here" removed
- Home screen: the menu button is no longer highlighted after a restart
- Web installer removed for now (returns with ready-built firmware)

<details><summary>Deutsch</summary>

- Neuanfang: Das Repository startet ohne alte Versionsgeschichte neu (aufgeräumt)
- Dock: Suchpause beim Wiederverbinden von TV/Streamer wieder aktiv (umgeht einen Absturz im Bluetooth-Controller des ESP32-C3)
- Konfigurator, Dockingstation: neuer Abschnitt „Bekannte Fehler“ – prüft täglich auf GitHub, ob Espressif den Fehler behoben hat, und meldet sich, wenn es sich lohnt
- Menü- und Startseiten-Editor: Optionen eines Eintrags in aufklappbaren Gruppen mit Kurzinfo
- Infrarot-Dialog einer Remote zeigt alle IR-Datenbanken und Meine Codes
- Seiten-Option „Lauter / Leiser / Stumm steuern hier“ entfernt
- Startseite: nach dem Neustart ist der Menü-Knopf nicht mehr markiert
- Web-Installer vorerst entfernt (kommt mit fertig gebauter Firmware zurück)

</details>

## 2026.10.28.1 (2026-10-28)

- New IR codes page: Flipper IRDB and Logitech Harmony databases (about 258,000 devices) kept in sync with GitHub, with automatic updates; "my codes" to learn, organise, test, collect from your keys, export and import
- Search across all sources from every key as well (filter per source); Harmony codes are recognised as protocols
- Other IR/RF blasters in Home Assistant work like a dock: any infrared entity, Broadlink (IR and 433 MHz), Xiaomi (IR)
- Home screen editor: its own editor like the menu editor, assignment per activity, built-in default page in the same editor; menu without home screens
- Popups with built-in number pad or touchpad
- Widgets: custom home screen lines up with the built-in one, single-line texts, full date
- Streamer app 1.8 with the new logo

<details><summary>Deutsch</summary>

- Neue Seite IR-Codes: Datenbanken Flipper-IRDB und Logitech Harmony (rund 258.000 Geräte) mit Abgleich gegen GitHub und automatischer Aktualisierung; Meine Codes zum Anlernen, Ordnen, Testen, Sammeln aus den Tasten, Export und Import
- Suche in allen Quellen auch in jeder Taste (Filter je Quelle), Harmony-Codes werden als Protokoll erkannt
- Fremde IR-/RF-Sender aus Home Assistant wie ein Dock: jede infrared-Entität, Broadlink (IR und 433 MHz), Xiaomi (IR)
- Startseiten-Editor: eigener Editor wie der Menü-Editor, Zuordnung je Aktivität, eingebaute Standardseite im selben Editor; Menü ohne Startseiten
- Popups mit eingebautem Ziffernblock oder Touchpad
- Widgets: Startseite sitzt wie die eingebaute, Texte einzeilig, Datum vollständig
- Streamer-App 1.8 mit neuem Logo

</details>

## 2026.10.27.3 (2026-10-27)

- Widgets for menu pages: clock, weather (with forecast), battery, media player, timer, camera, combined – all selectable in the configurator
- Own home screen per activity (as free as the menu, or from a template)
- Load factory settings / load default menu / load default keys
- Simpler menu editor: new pages are linked automatically, drag in the tree to move, path display, popup list only shows real popups
- Popups: top-left button can minimise instead of close
- Configurator: turn-on-Wi-Fi button, choose the action before picking an entity, readable dropdowns in dark mode, less flicker/re-rendering, firmware page no longer jumps
- Dock: connection retries, passive scan

<details><summary>Deutsch</summary>

- Widgets für Menüseiten: Uhr, Wetter (mit Vorhersage), Akku, Medien-Player, Timer, Kamera, Kombi – alles im Konfigurator auswählbar
- Eigene Startseite je Aktivität (frei wie das Menü, oder aus Vorlage)
- Werkseinstellung laden / Standardmenü laden / Standardtasten laden
- Menü-Editor einfacher: neue Seiten werden automatisch verlinkt, Ziehen im Baum zum Umhängen, Pfadanzeige, Popups-Liste nur noch mit echten Popups
- Popups: Knopf oben links wahlweise Minimieren statt Schließen
- Konfigurator: WLAN-einschalten-Knopf, Aktion wählen vor der Entitätsauswahl, Dark-Mode-Auswahllisten lesbar, weniger Flackern/Neuaufbau, Firmware-Seite springt nicht mehr
- Dock: Verbindungsaufbau mit Wiederholung, passiver Scan

</details>

## 2026.10.27.2 (2026-10-27)

- **Everything is now called ESPHomeRemote:** `esphomeremote.yaml` / `esphomeremote-firmware.yaml` (remote) and `esphomeremote-dock.yaml` / `esphomeremote-dock-firmware.yaml` (dock); the headers are called `esphomeremote-*.h`. New devices are named `esphomeremote` by default.
- **Existing devices keep building:** the repository still ships the old firmware names (`open-remote-firmware.yaml`, `open-dock-firmware.yaml`) for now. In the configurator under *Firmware*, **Umstellen** (switch over) changes the device file to the new names (the old one is kept as `.alt`). Device name and entities stay as they are.

<details><summary>Deutsch</summary>

- **Alles heißt jetzt ESPHomeRemote:** `esphomeremote.yaml` / `esphomeremote-firmware.yaml` (Fernbedienung) und `esphomeremote-dock.yaml` / `esphomeremote-dock-firmware.yaml` (Dock), die Header heißen `esphomeremote-*.h`. Neue Geräte heißen standardmäßig `esphomeremote`.
- **Bestehende Geräte bauen weiter:** Die alten Firmware-Namen (`open-remote-firmware.yaml`, `open-dock-firmware.yaml`) liefert das Repo vorerst mit. Im Konfigurator unter *Firmware* zeigt **Umstellen** die Gerätedatei auf die neuen Namen um (die alte bleibt als `.alt`). Gerätename und Entitäten bleiben dabei, wie sie sind.

</details>

## 2026.10.27.1 (2026-10-27)

- **Important for HACS:** the repository now contains only the ESPHomeRemote integration. Previously HACS accidentally installed and updated the Bluetooth bridge ("ESPHome BLE-Link") instead of the configurator. Please remove the repository from HACS once and add it again, then download ESPHomeRemote and restart Home Assistant.
- The Bluetooth bridge is now part of ESPHomeRemote (no separate integration any more). If you have `custom_components/esphome_ble_link`: delete it and remove `esphome_ble_link:` from configuration.yaml / your packages.
- New icon and logo (Home Assistant, HACS, GitHub).

<details><summary>Deutsch</summary>

- **Wichtig für HACS:** Im Repo liegt jetzt nur noch die Integration ESPHomeRemote. Vorher hat HACS versehentlich die Bluetooth-Brücke ("ESPHome BLE-Link") statt des Konfigurators installiert und aktualisiert. Bitte das Repo in HACS einmal entfernen und neu hinzufügen, danach ESPHomeRemote herunterladen und Home Assistant neu starten.
- Die Bluetooth-Brücke ist jetzt Teil von ESPHomeRemote (keine eigene Integration mehr). Falls du `custom_components/esphome_ble_link` hast: löschen, und `esphome_ble_link:` aus der configuration.yaml / den Paketen entfernen.
- Neues Icon und Logo (Home Assistant, HACS, GitHub).

</details>

## 2026.10.26.6 (2026-10-26)

Docks are always recognised.

- A dock now counts as a dock even if its name doesn't start with `dock-…`: it is recognised by its firmware (project `aggro600.esphomeremote-dock`). Manually set-up docks therefore appear in the configurator, and the radio link remote → dock and the permission for HA actions apply to them too.
- *Firmware & Geräte* (firmware & devices) finds dock files under any file name.

<details><summary>Deutsch</summary>

Docks werden immer erkannt.

- Ein Dock zählt jetzt auch dann als Dock, wenn es nicht `dock-…` heißt: erkannt an seiner Firmware (Projekt `aggro600.esphomeremote-dock`). Von Hand eingerichtete Docks erscheinen damit im Konfigurator, der Funkweg Remote → Dock und die Freigabe der HA-Aktionen gelten auch für sie.
- *Firmware & Geräte* findet Dock-Dateien unter jedem Dateinamen.

</details>

## 2026.10.26.5 (2026-10-26)

Help with setting up.

- **Repair old files:** under *Firmware & Geräte*, dock and remote files in the old format are marked (dock with `!include templates/dock-basis.yaml`, remote with `@master` without passed-through passwords). **"Auf GitHub-Firmware umstellen"** (switch to GitHub firmware) rewrites them correctly; the old one is kept as `.alt`.
- **Not adopted:** if a remote isn't adopted in Home Assistant, the configurator shows a clear notice at the top with the steps – without this connection, assignment and layout don't arrive.
- The firmware page's hint text now also fits without developer files.

<details><summary>Deutsch</summary>

Hilfe beim Einrichten.

- **Alte Dateien reparieren:** Unter *Firmware & Geräte* sind Dock- und Remote-Dateien in alter Form markiert (Dock mit `!include templates/dock-basis.yaml`, Remote mit `@master` ohne durchgereichte Passwörter). **„Auf GitHub-Firmware umstellen“** schreibt sie richtig um, die alte bleibt als `.alt`.
- **Nicht eingebunden:** Ist eine Remote nicht in Home Assistant eingebunden, zeigt der Konfigurator oben einen deutlichen Hinweis mit den Schritten – ohne diese Verbindung kommen Belegung und Layout nicht an.
- Hinweistext der Firmware-Seite passt jetzt auch ohne Entwickler-Dateien.

</details>

## 2026.10.26.4 (2026-10-26)

No more "Action esphome.…_tasten_neu_laden not found" error.

- After saving, the configurator only nudges the remote if it is currently connected directly to Home Assistant. Otherwise it fetches the new assignment itself the next time it wakes up (via dock or HA).
- All actions on remotes and docks (Bluetooth, infrared test, learning …) now report errors only as an understandable notice in the configurator.
- Note: if the action is permanently missing, the remote isn't adopted in Home Assistant yet – README, step 5.

<details><summary>Deutsch</summary>

Keine Fehlermeldung „Action esphome.…_tasten_neu_laden not found“ mehr.

- Nach dem Speichern stößt der Konfigurator die Remote nur noch an, wenn sie gerade direkt mit Home Assistant verbunden ist. Sonst holt sie sich die neue Belegung beim nächsten Aufwachen selbst (über Dock bzw. HA).
- Alle Aktionen an Remotes und Docks (Bluetooth, Infrarot-Test, Anlernen …) melden Fehler nur noch als verständlichen Hinweis im Konfigurator.
- Hinweis: Fehlt die Aktion dauerhaft, ist die Remote noch nicht in Home Assistant eingebunden – README, Schritt 5.

</details>

## 2026.10.26.3 (2026-10-26)

Setting up without developer files.

- **Docking station:** new include file `open-dock.yaml` – the dock firmware comes straight from GitHub, like the remote's. In the configurator, "Neues Dock" (new dock) creates this form by itself (before: "templates/dock-basis.yaml missing").
- **New remote** in the configurator now also works without a developer setup; if entries are missing in `secrets.yaml`, the assistant tells you which.
- **Remote:** passwords come from your own `secrets.yaml` via `substitutions` (GitHub packages may not read `!secret`) – if you copied `open-remote.yaml` earlier, please take the new version.

<details><summary>Deutsch</summary>

Einrichten ohne Entwickler-Dateien.

- **Dockingstation:** neue Einbinde-Datei `open-dock.yaml` – die Dock-Firmware kommt wie bei der Fernbedienung direkt von GitHub. Im Konfigurator legt „Neues Dock“ diese Form selbst an (vorher: „templates/dock-basis.yaml fehlt“).
- **Neue Fernbedienung** im Konfigurator geht jetzt auch ohne Entwickler-Einrichtung; fehlen Einträge in der `secrets.yaml`, sagt der Assistent welche.
- **Fernbedienung:** Passwörter kommen über `substitutions` aus der eigenen `secrets.yaml` (GitHub-Pakete dürfen kein `!secret` lesen) – wer `open-remote.yaml` früher übernommen hat, bitte die neue Fassung nehmen.

</details>

## 2026.10.26.2 (2026-10-26)

First release with version numbers.

- **Updates for everyone:** Home Assistant announces new versions via HACS; in the configurator under *Firmware*, "Neue Version" shows which remotes and docks are outdated – **Alle aktualisieren** (update all) builds and flashes. Queuing builds right away, "Nur bauen" (build only) for quick flashing later.
- **Firmware from GitHub:** `open-remote.yaml` fetches it from the `stabil` branch; passwords come from your own `secrets.yaml` (take the new version of `open-remote.yaml`).
- **Streamer app 1.7:** start page on the TV (status, uninstall); set up, update and remove it in the configurator – the app ships with the integration. Cursor ‹ › in text fields.
- **Media popup** made of separate, freely movable elements, popup templates, tile options (layout, second line, badge, colours per state, condition, long press, confirmation …).
- **Timer:** several selectable, popup "timer finished" with +5 min and again.
- **Keyboard:** holding repeats, keyboard popup more reliable and faster.
- **Configurator:** copy and paste in parts, take parts from other remotes, backup with settings, new battery page, dock traffic light, home screen buttons with their own target.

<details><summary>Deutsch</summary>

Erster Release mit Versionsnummern.

- **Updates für alle:** Home Assistant meldet neue Versionen über HACS; im Konfigurator unter *Firmware* zeigt „Neue Version“, welche Fernbedienungen und Docks veraltet sind – **Alle aktualisieren** baut und spielt auf. Vormerken baut sofort, „Nur bauen“ für schnelles Flashen danach.
- **Firmware von GitHub:** `open-remote.yaml` holt sie vom Zweig `stabil`; Passwörter kommen aus der eigenen `secrets.yaml` (neue Fassung von `open-remote.yaml` übernehmen).
- **Streamer-App 1.7:** Start-Seite am Fernseher (Status, Deinstallieren), im Konfigurator einrichten, aktualisieren und entfernen – die App kommt mit der Integration. Cursor ‹ › im Eingabefeld.
- **Medien-Popup** aus einzelnen, frei verschiebbaren Elementen, Popup-Vorlagen, Kachel-Optionen (Anordnung, zweite Zeile, Abzeichen, Farben je Zustand, Bedingung, Lang drücken, Sicherheitsabfrage …).
- **Timer:** mehrere auswählbar, Popup „Timer abgelaufen“ mit +5 min und Nochmal.
- **Tastatur:** Halten wiederholt, Tastatur-Popup zuverlässiger und schneller.
- **Konfigurator:** Kopieren und Einfügen in Teilen, Teile von anderen Remotes übernehmen, Sicherung mit Einstellungen, neue Akku-Seite, Dock-Ampel, Startseiten-Knöpfe mit eigenem Ziel.

</details>
