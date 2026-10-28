# Was ist neu

## 2026.10.28.2 (28.10.2026)

- Neuanfang: Das Repository startet ohne alte Versionsgeschichte neu (aufgeräumt)
- Dock: Suchpause beim Wiederverbinden von TV/Streamer wieder aktiv (umgeht einen Absturz im Bluetooth-Controller des ESP32-C3)
- Konfigurator, Dockingstation: neuer Abschnitt „Bekannte Fehler“ – prüft täglich auf GitHub, ob Espressif den Fehler behoben hat, und meldet sich, wenn es sich lohnt
- Menü- und Startseiten-Editor: Optionen eines Eintrags in aufklappbaren Gruppen mit Kurzinfo
- Infrarot-Dialog einer Remote zeigt alle IR-Datenbanken und Meine Codes
- Seiten-Option „Lauter / Leiser / Stumm steuern hier“ entfernt
- Startseite: nach dem Neustart ist der Menü-Knopf nicht mehr markiert
- Web-Installer vorerst entfernt (kommt mit fertig gebauter Firmware zurück)

## 2026.10.28.1 (28.10.2026)

- Neue Seite IR-Codes: Datenbanken Flipper-IRDB und Logitech Harmony (rund 258.000 Geräte) mit Abgleich gegen GitHub und automatischer Aktualisierung; Meine Codes zum Anlernen, Ordnen, Testen, Sammeln aus den Tasten, Export und Import
- Suche in allen Quellen auch in jeder Taste (Filter je Quelle), Harmony-Codes werden als Protokoll erkannt
- Fremde IR-/RF-Sender aus Home Assistant wie ein Dock: jede infrared-Entität, Broadlink (IR und 433 MHz), Xiaomi (IR)
- Startseiten-Editor: eigener Editor wie der Menü-Editor, Zuordnung je Aktivität, eingebaute Standardseite im selben Editor; Menü ohne Startseiten
- Popups mit eingebautem Ziffernblock oder Touchpad
- Widgets: Startseite sitzt wie die eingebaute, Texte einzeilig, Datum vollständig
- Streamer-App 1.8 mit neuem Logo

## 2026.10.27.3 (27.10.2026)

- Widgets für Menüseiten: Uhr, Wetter (mit Vorhersage), Akku, Medien-Player, Timer, Kamera, Kombi – alles im Konfigurator auswählbar
- Eigene Startseite je Aktivität (frei wie das Menü, oder aus Vorlage)
- Werkseinstellung laden / Standardmenü laden / Standardtasten laden
- Menü-Editor einfacher: neue Seiten werden automatisch verlinkt, Ziehen im Baum zum Umhängen, Pfadanzeige, Popups-Liste nur noch mit echten Popups
- Popups: Knopf oben links wahlweise Minimieren statt Schließen
- Konfigurator: WLAN-einschalten-Knopf, Aktion wählen vor der Entitätsauswahl, Dark-Mode-Auswahllisten lesbar, weniger Flackern/Neuaufbau, Firmware-Seite springt nicht mehr
- Dock: Verbindungsaufbau mit Wiederholung, passiver Scan

## 2026.10.27.2 (27.10.2026)

- **Alles heißt jetzt ESPHomeRemote:** `esphomeremote.yaml` / `esphomeremote-firmware.yaml` (Fernbedienung) und `esphomeremote-dock.yaml` / `esphomeremote-dock-firmware.yaml` (Dock), die Header heißen `esphomeremote-*.h`. Neue Geräte heißen standardmäßig `esphomeremote`.
- **Bestehende Geräte bauen weiter:** Die alten Firmware-Namen (`open-remote-firmware.yaml`, `open-dock-firmware.yaml`) liefert das Repo vorerst mit. Im Konfigurator unter *Firmware* zeigt **Umstellen** die Gerätedatei auf die neuen Namen um (die alte bleibt als `.alt`). Gerätename und Entitäten bleiben dabei, wie sie sind.

## 2026.10.27.1 (27.10.2026)

- **Wichtig für HACS:** Im Repo liegt jetzt nur noch die Integration ESPHomeRemote. Vorher hat HACS versehentlich die Bluetooth-Brücke ("ESPHome BLE-Link") statt des Konfigurators installiert und aktualisiert. Bitte das Repo in HACS einmal entfernen und neu hinzufügen, danach ESPHomeRemote herunterladen und Home Assistant neu starten.
- Die Bluetooth-Brücke ist jetzt Teil von ESPHomeRemote (keine eigene Integration mehr). Falls du `custom_components/esphome_ble_link` hast: löschen, und `esphome_ble_link:` aus der configuration.yaml / den Paketen entfernen.
- Neues Icon und Logo (Home Assistant, HACS, GitHub).

## 2026.10.26.6 (26.10.2026)

Docks werden immer erkannt.

- Ein Dock zählt jetzt auch dann als Dock, wenn es nicht `dock-…` heißt: erkannt an seiner Firmware (Projekt `aggro600.esphomeremote-dock`). Von Hand eingerichtete Docks erscheinen damit im Konfigurator, der Funkweg Remote → Dock und die Freigabe der HA-Aktionen gelten auch für sie.
- *Firmware & Geräte* findet Dock-Dateien unter jedem Dateinamen.

## 2026.10.26.5 (26.10.2026)

Hilfe beim Einrichten.

- **Alte Dateien reparieren:** Unter *Firmware & Geräte* sind Dock- und Remote-Dateien in alter Form markiert (Dock mit `!include templates/dock-basis.yaml`, Remote mit `@master` ohne durchgereichte Passwörter). **„Auf GitHub-Firmware umstellen“** schreibt sie richtig um, die alte bleibt als `.alt`.
- **Nicht eingebunden:** Ist eine Remote nicht in Home Assistant eingebunden, zeigt der Konfigurator oben einen deutlichen Hinweis mit den Schritten – ohne diese Verbindung kommen Belegung und Layout nicht an.
- Hinweistext der Firmware-Seite passt jetzt auch ohne Entwickler-Dateien.

## 2026.10.26.4 (26.10.2026)

Keine Fehlermeldung „Action esphome.…_tasten_neu_laden not found“ mehr.

- Nach dem Speichern stößt der Konfigurator die Remote nur noch an, wenn sie gerade direkt mit Home Assistant verbunden ist. Sonst holt sie sich die neue Belegung beim nächsten Aufwachen selbst (über Dock bzw. HA).
- Alle Aktionen an Remotes und Docks (Bluetooth, Infrarot-Test, Anlernen …) melden Fehler nur noch als verständlichen Hinweis im Konfigurator.
- Hinweis: Fehlt die Aktion dauerhaft, ist die Remote noch nicht in Home Assistant eingebunden – README, Schritt 5.

## 2026.10.26.3 (26.10.2026)

Einrichten ohne Entwickler-Dateien.

- **Dockingstation:** neue Einbinde-Datei `open-dock.yaml` – die Dock-Firmware kommt wie bei der Fernbedienung direkt von GitHub. Im Konfigurator legt „Neues Dock“ diese Form selbst an (vorher: „templates/dock-basis.yaml fehlt“).
- **Neue Fernbedienung** im Konfigurator geht jetzt auch ohne Entwickler-Einrichtung; fehlen Einträge in der `secrets.yaml`, sagt der Assistent welche.
- **Fernbedienung:** Passwörter kommen über `substitutions` aus der eigenen `secrets.yaml` (GitHub-Pakete dürfen kein `!secret` lesen) – wer `open-remote.yaml` früher übernommen hat, bitte die neue Fassung nehmen.

## 2026.10.26.2 (26.10.2026)

Erster Release mit Versionsnummern.

- **Updates für alle:** Home Assistant meldet neue Versionen über HACS; im Konfigurator unter *Firmware* zeigt „Neue Version“, welche Fernbedienungen und Docks veraltet sind – **Alle aktualisieren** baut und spielt auf. Vormerken baut sofort, „Nur bauen“ für schnelles Flashen danach.
- **Firmware von GitHub:** `open-remote.yaml` holt sie vom Zweig `stabil`; Passwörter kommen aus der eigenen `secrets.yaml` (neue Fassung von `open-remote.yaml` übernehmen).
- **Streamer-App 1.7:** Start-Seite am Fernseher (Status, Deinstallieren), im Konfigurator einrichten, aktualisieren und entfernen – die App kommt mit der Integration. Cursor ‹ › im Eingabefeld.
- **Medien-Popup** aus einzelnen, frei verschiebbaren Elementen, Popup-Vorlagen, Kachel-Optionen (Anordnung, zweite Zeile, Abzeichen, Farben je Zustand, Bedingung, Lang drücken, Sicherheitsabfrage …).
- **Timer:** mehrere auswählbar, Popup „Timer abgelaufen“ mit +5 min und Nochmal.
- **Tastatur:** Halten wiederholt, Tastatur-Popup zuverlässiger und schneller.
- **Konfigurator:** Kopieren und Einfügen in Teilen, Teile von anderen Remotes übernehmen, Sicherung mit Einstellungen, neue Akku-Seite, Dock-Ampel, Startseiten-Knöpfe mit eigenem Ziel.
