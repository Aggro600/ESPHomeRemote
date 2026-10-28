<p align="center">🇬🇧 <a href="README.md">English</a> · <b>🇩🇪 <a href="README.de.md">Deutsch</a></b></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/bilder/logo-dunkel.png">
    <img src="docs/bilder/logo.png" alt="ESPHomeRemote" width="560">
  </picture>
</p>

# ESPHomeRemote

**Eine Fernbedienung für Fernseher, Streamer und dein ganzes Smart Home** – mit Touchdisplay,
Bluetooth und Sprachassistent, gebaut auf der *OMOTE / Open Remote Rev6*-Platine und komplett in
**ESPHome**. Tasten, Menü und Startseite stellst du bequem im Browser in **Home Assistant** ein,
ohne zu programmieren.

> Du brauchst keine Programmierkenntnisse. Wenn du Home Assistant schon benutzt, bist du in etwa
> 30 Minuten fertig.

<p align="center"><a href="https://aggro600.github.io/ESPHomeRemote/showcase/"><img src="docs/bilder/showcase.png" alt="3D-Showcase: OpenRemote Rev6 im Dock mit Smart Display" width="900"></a></p>
<p align="center"><b><a href="https://aggro600.github.io/ESPHomeRemote/showcase/">▶ Remote und Dockingstation in 3D ansehen</a></b><br>
<sub>Interaktiv im Browser: aus den Original-CAD-Daten, mit der echten Display-Oberfläche, Andocken, Laden und Explosionsansicht</sub></p>

---

## Was sie kann

<p align="center"><img src="docs/bilder/uebersicht.png" alt="Konfigurator: Übersicht" width="900"></p>

**Fernbedienung**
- **Steuert Fernseher, Streamer, Tablets und PCs per Bluetooth** – 4 Geräte, Umschalten über „Aktivitäten“
  wie bei Harmony. Je Gerät wählbar: nur Tasten, **Maus (Touchpad)**, **Tastatur** oder beides.
- **Jede Taste frei belegbar** – kurz, doppelt und lang getrennt, je Aktivität anders, mit Halten und Wiederholen.
  Dazu Schritte **beim Starten/Beenden** einer Aktivität (TV an, Eingang wählen …).
- **Folgemodi** – nach einer Aktion gelten ein paar Sekunden andere Tasten (z. B. Kanal +/− dimmt die Lampe).
- **Bewegungsgesten** – zweimal schütteln, kippen, und **eigene Gesten anlernen** (Kreis, Wischen …):
  erkannt wird die Form, egal wie groß, schnell oder wie man die Remote hält.
- **Touchpad** – das Display wird zur Maus, mit Scrollstreifen, Klicktasten und Tastenkürzel-Makros.
- **Infrarot** – rund 270.000 Geräte aus der Flipper-IRDB und dem Logitech-Harmony-Archiv (bleiben per GitHub aktuell),
  eigene Codes **anlernen** und verwalten; senden über die Remote, das Dock oder andere IR-Sender in Home Assistant (Broadlink …).
- **Sprachassistent** – Google auf dem Fernseher oder Home Assistant Assist.

**Display & Smart Home**
- **Eigenes Menü** – Seiten, Kacheln, Licht-Regler, Rollos, Anzeigen, Kamerabilder im Raster frei anordnen,
  dazu **Popups**, die von selbst aufgehen („Es hat geklingelt“, Sleeptimer, Bildmodus bei Netflix …).
- **Smart Display im Dock** – steht die Remote im Dock, zeigt sie Uhrzeit, Wetter, Raumklima, Termine und Ladestand.
- **Helligkeit nach Lichtsensor** – Display, Tastenlicht und Dock-LED folgen dem Raumlicht; die Kurve lernt mit.
- **Kamera und Klingel** – Bild auf der Fernbedienung, Kamera auf dem Fernseher (in drei Größen).
- **Lange Akkulaufzeit** – Tiefschlaf, wacht auf beim Hochheben, Klopfen oder Tastendruck; die Weck-Taste und alles,
  was während des Starts gedrückt wird, kommt trotzdem an.

**Dockingstation (optional, ESP32-C3)**
- **IR-Sender für Home Assistant** – jedes IR-Gerät wird ein Media-Player, eine Remote-Entität
  (`remote.send_command` mit Harmony-Befehlsnamen) und je Befehl ein Button. Dazu **433 MHz** (CC1101).
- **Aktivitäts-Schalter** – je Remote und Aktivität ein Schalter in Home Assistant.
- **Funkbrücke zu Home Assistant** – Tastenbefehle, Stand (Player, Fernseher, Timer) und Cover gehen per ESP-NOW
  verschlüsselt übers Dock: sofort nach dem Aufwachen, ohne dass die Remote erst eine Verbindung aufbaut.
  Im Konfigurator wählbar: **WLAN**, **Bluetooth**, **Dock + Bluetooth** oder **Dock, sparsam**.

**Konfigurator in Home Assistant** – alles im Browser, am PC und am Handy:
Übersicht mit Akkuverlauf, **Suche mit Strg+K**, Tabelle aller Tasten, Änderungsliste mit Rückgängig,
Sicherung, **Firmware-Updates per WLAN** (auch vormerken, wenn die Remote schläft) und **USB-Flashen im Browser**.
Einstellungen gleichen sich in beide Richtungen ab: was du an der Remote umstellst, sieht der Konfigurator.

---

## Bilder

| | |
|---|---|
| <img src="docs/bilder/kreisgeste.png" alt="Eigene Geste: Kreis"> | <img src="docs/bilder/lichtsensor.png" alt="Helligkeit nach Lichtsensor"> |
| **Eigene Geste „Kreis“** – dreimal an der Remote ausgeführt, die Proben (X/Y/Z) im Konfigurator. | **Helligkeit nach Lichtsensor** – Punkte ziehen, die Kurve lernt beim Verstellen mit. |
| <img src="docs/bilder/tasten.png" alt="Tasten belegen"> | <img src="docs/bilder/menue.png" alt="Menü-Editor"> |
| **Tasten** – Fernbedienung antippen, je Druckart Aktionen anlegen. | **Menü-Editor** – mit Live-Vorschau des Displays. |
| <img src="docs/bilder/gesten.png" alt="Gesten"> | <img src="docs/bilder/tabelle.png" alt="Tabelle"> |
| **Gesten** – eingebaute und eigene, „wirkt wie Taste“ oder eigene Aktionen. | **Tabelle** – alle Aktivitäten und Folgemodi nebeneinander. |
| <img src="docs/bilder/dock.png" alt="Dockingstation"> | <img src="docs/bilder/suche.png" alt="Suche"> |
| **Dockingstation** – Dock-LED, Smart Display mit Vorschau, IR-Geräte. | **Suche (Strg+K)** – findet jede Taste, Einstellung und Menüseite. |
| <img src="docs/bilder/bluetooth.png" alt="Bluetooth"> | <img src="docs/bilder/handy.png" alt="Am Handy" width="300"> <img src="docs/bilder/handy_tasten.png" alt="Tasten am Handy" width="300"> |
| **Bluetooth** – vier Plätze, Eingabe je Gerät, Maus-Einstellungen. | **Am Handy** – gleiche Funktionen, Leiste unten. |

---

## Was du brauchst

- Die Fernbedienung: **OMOTE / Open Remote, Platine Rev6** (ESP32-S3).
  Hardware: https://github.com/OMOTE-Community/OMOTE-Hardware
  Du bekommst die PCBs evtl. über den Discord buy/sell: https://discord.com/invite/5PnYFAsKsG
  
- Ein **USB-C-Kabel** und einen PC mit **Chrome oder Edge** (zum ersten Aufspielen).
- Für alle Funktionen: **Home Assistant** mit dem **ESPHome**-Add-on (oder „ESPHome Device Builder“)
  und **HACS** (für den Konfigurator).

---

## Schnellstart

> **Web-Installer (ohne Home Assistant):** kommt zurück, sobald es fertig gebaute Firmware zum Herunterladen gibt.
> Bis dahin geht es über Home Assistant (unten).

### Mit Home Assistant (empfohlen)

**Schritt 1 – ESPHome bereitlegen**
In Home Assistant: *Einstellungen → Add-ons → Add-on-Store →* **ESPHome Device Builder** installieren
und starten.

**Schritt 2 – Konfigurator installieren (über HACS)**
1. Falls noch nicht da: **HACS** installieren (https://hacs.xyz – einmalig, 5 Minuten).
2. In Home Assistant **HACS** öffnen → oben rechts **⋮** → **„Benutzerdefinierte Repositories“**.
3. Einfügen: `https://github.com/Aggro600/ESPHomeRemote` – Typ **„Integration“** → **Hinzufügen**.
4. In HACS nach **„ESPHomeRemote“** suchen → **Herunterladen**.
5. Home Assistant **neu starten** (*Einstellungen → System → oben rechts ⏻ → Neu starten*).
6. *Einstellungen → Geräte & Dienste →* **„+ Integration hinzufügen“** → **„ESPHomeRemote“**.
   Den Namen der Fernbedienung so lassen (`esphomeremote`) → **Absenden**.
7. Es erscheint eine Zeile wie `esphomeremote_konfig_key: "…"` – **kopieren**, die brauchst du gleich.

**Schritt 3 – Passwörter eintragen**
Im ESPHome-Add-on oben rechts auf **„Secrets“** klicken und diese Zeilen ergänzen
(WLAN steht meist schon drin, die Passwörter sind frei wählbar):
```yaml
ota_pw: "ein-passwort"
ap_password: "noch-ein-passwort"
direkt_schluessel: "…"             # 32 Hex-Zeichen, z. B. von  openssl rand -hex 16  (Funkweg zum Dock)
esphomeremote_konfig_key: "…"      # die kopierte Zeile aus Schritt 2
```

**Schritt 4 – Fernbedienung aufspielen**
1. Im ESPHome-Add-on **„+ Neues Gerät“** → **„Überspringen“** (bzw. „Leer anlegen“).
2. Den Inhalt von [`esphomeremote.yaml`](esphomeremote.yaml) in den Editor kopieren – anpassen musst du nichts
   (die Adresse von Home Assistant findet die Fernbedienung selbst).
3. **Speichern** → **Installieren** → *„An diesen Computer anschließen“* → Fernbedienung per USB-C
   anstecken und auswählen. Das erste Mal dauert es etwa 10 Minuten.

**Schritt 5 – In Home Assistant übernehmen**
1. Home Assistant meldet *„Neues Gerät gefunden“* (*Einstellungen → Geräte & Dienste*) → **Hinzufügen**.
2. Beim Gerät auf **„Konfigurieren“** → **„Gerät darf Home-Assistant-Aktionen ausführen“** einschalten.
3. Fertig! Links in der Seitenleiste ist jetzt **„Fernbedienung“** – dort stellst du alles ein.

---

### Streamer-App (optional)

Für Titel, Cover und das Tastatur-Popup von einem Android-TV-Streamer: am Streamer die Entwickleroptionen und
USB-Debugging einschalten, in Home Assistant die Integration **Android Debug Bridge** mit seiner IP hinzufügen, dann
im Konfigurator *Wiedergabe → Streamer anlegen* → **Einrichten**. Die App kommt mit der Integration mit; Updates und
Deinstallieren gehen dort genauso. Auf dem Fernseher zeigt sie unter „ESPHomeRemote Streamer“, ob alles läuft.

### Updates

- **Konfigurator:** Home Assistant meldet neue Versionen wie bei jeder HACS-Integration (*Einstellungen → Updates*),
  mit der Liste, was neu ist. Installieren, Home Assistant neu starten – fertig.
- **Fernbedienungen und Docks:** danach im Konfigurator unter *Firmware* steht „Neue Version …“ –
  **Alle aktualisieren** baut und spielt sie auf (schlafende Remotes kurz anfassen).
- Die Firmware kommt vom Zweig `stabil` (letzter Release).
- **Alte Dateinamen:** Bis 27.10.2026 hießen die Dateien `open-remote.yaml` / `open-remote-firmware.yaml` und
  `open-dock.yaml` / `open-dock-firmware.yaml`. Die alten Firmware-Namen funktionieren vorerst weiter; im
  Konfigurator unter *Firmware* stellt **Umstellen** die Gerätedatei auf die neuen Namen um (die alte bleibt als
  `.alt` liegen). Von Hand: den `packages:`-Block durch den aus [`esphomeremote.yaml`](esphomeremote.yaml) ersetzen.
- Was neu ist: [CHANGELOG.md](CHANGELOG.md).


## Der Konfigurator

In der Seitenleiste **„Fernbedienung“** (am Handy und am PC). Links stehen deine Fernbedienungen mit Akkustand,
darunter je Remote ihre Seiten:

| Seite | Was du dort machst |
|---|---|
| **Übersicht** | Alle Remotes und Docks: Akku mit 24-h-Verlauf, Verbindung, Firmware, Hinweise mit Knöpfen zum Erledigen. |
| **Tasten** | Jede Taste je Aktivität belegen: kurz / doppelt / lang. Bluetooth, Home-Assistant-Aktion, Infrarot, 433 MHz, Funktion der Remote. Aktivitäten und Folgemodi anlegen, Tabellenansicht. |
| **Startseite** | Je Aktivität eine eigene Startseite mit Einträgen und Widgets (Uhr, Wetter, Medien, Timer, Kamera) – oder die eingebaute anpassen. |
| **Menü** | Seiten, Unterseiten und Popups im Raster gestalten, mit Vorschau. |
| **Gesten** | Schütteln, Kippen und eigene Gesten belegen; Touchpad-Makros. |
| **Bluetooth / Infrarot** | Plätze koppeln, Eingabe je Gerät, IR-Sendeweg (Remote, Dock, andere IR-Sender). |
| **Verknüpfungen** | Welche Geräte aus Home Assistant die Remote nutzt: Wiedergabe-Player (Startseite, Media-Seite, Musik-Anzeige), Fernseher, Multiroom, Lautsprecher für den Assistenten, Streamer für Texteingabe, Timer neben der Uhr. |
| **Einstellungen** | Alle Einstellungen der Remote, sofort übertragen; Helligkeit nach Lichtsensor. |
| **Wiedergabe** | „Jetzt läuft“-Player anlegen: fasst Fernseher, Streamer, Multiroom-Musik und Chromecast zu einem Media Player zusammen. Streamer mit der App „ESPHomeRemote Streamer“ (Titel, Cover, Position jeder App, Tastatur-Popup). |
| **Klingel** | Bis zu zwei Klingeln: Auslöser und Kamera – beim Klingeln kommt das Bild auf die Remotes. |
| **Dockingstation** | IR-Geräte, Dock-LED, Smart Display, bekannte Fehler. |
| **IR-Codes** | IR-Datenbanken (Flipper, Harmony) mit Abgleich gegen GitHub, eigene Codes anlernen, ordnen, exportieren. |
| **Firmware & Geräte** | Updates per WLAN oder USB, neue Docks anlegen und einbinden. |
| **Werkstatt** | Neue Fernbedienung, alles kopieren, Sicherung, Platinen-Test, Entwicklermodus. |

**Speichern** (oder Strg+S) schickt alles an die Fernbedienung – schläft sie, holt sie es beim nächsten Aufwachen ab.

---

## Bedienung in Kürze

- **Menü-Taste**: Menü öffnen / schließen. Doppelt: Display aus.
- **Steuerkreuz + OK**: im Menü bewegen, auf der Startseite steuert es das aktive Gerät.
- **Zurück**: eine Seite zurück, in Popups schließen.
- **Leiste unten auf der Startseite**: zwischen Fernseher, Streamer usw. umschalten.
- **Bluetooth koppeln**: Menü → Einstellungen → Bluetooth → Platz wählen → „Neu koppeln“,
  dann am Fernseher / Streamer nach „ESPHomeRemote“ suchen.

---

## Häufige Fragen

**Die Fernbedienung schaltet in Home Assistant nichts.**
Beim ESPHome-Gerät „Gerät darf Home-Assistant-Aktionen ausführen“ einschalten (Schritt 5).

**Im Konfigurator steht „noch nicht übertragen“.**
Die Fernbedienung schläft. Einmal eine Taste drücken – sie holt die Belegung dann ab.
Klappt es nie: Stimmt `esphomeremote_konfig_key` in den ESPHome-Secrets mit dem Schlüssel der
Integration überein (*ESPHomeRemote → Konfigurieren* zeigt ihn) und `ha_host` in der YAML?

**Mehrere Fernbedienungen?**
Für jede in ESPHome ein eigenes Gerät mit eigenem `device_name`/`ha_name` anlegen (Schritt 4).
Dann bei der Integration *ESPHomeRemote* auf **„Konfigurieren“** und den neuen Namen mit Komma ergänzen.

**Seiten zeigen „nicht verfügbar“.**
Einige eingebaute Seiten (Startseite-Player, Klingel, Sleeptimer) nutzen die Entitäten aus
`esphomeremote.yaml` → `substitutions`. Trage dort deine ein oder lass sie weg – das Menü baust du
ohnehin im Konfigurator.

**Kamera auf dem Fernseher in Klein / Mittel / Groß** (optional):
Beispiel in `home-assistant/packages/beispiel_kamera_tv.yaml` und die Blaupause in
`home-assistant/blueprints/script/esphomeremote/`.

---

## Für Entwickler

- `esphomeremote-firmware.yaml` wird aus der Entwickler-Fassung erzeugt (`tools/veroeffentlichen.py`
  im Entwickler-Setup) – Änderungen bitte dort, nicht hier.
- Eigene Komponenten in `components/`: Display (`ili9341_i80`), Tastenfeld (`tca8418`), Akku
  (`max17048`), Lagesensor (`lis3dh`), Bluetooth-HID (`espidf_ble_keyboard`, `atv_voice`),
  Konfigurator (`tasten_konfig`, `menue`), SD-Karte, Touch (`ft63x6`).
- `custom_components/esphomeremote_konfig`: Integration mit Panel
  (`frontend/konfigurator.js`), liefert die Belegung per HTTP an die Fernbedienung.

**Entwickler-Fassung (1:1 wie im Einsatz)** – zusätzlich zur einfachen Fassung oben liegt hier alles, womit die
Remotes tatsächlich gebaut werden:
- `templates/remote.yaml` (Firmware) und `templates/remote-halink.yaml` (Bluetooth-Schicht „Nur Bluetooth“,
  HA über BLE/Dock – diese Datei wird geflasht), `templates/dock-basis.yaml` (Dockingstation),
  `remote-dev-tests.yaml` (Testwerkzeuge, abschaltbar).
- `tools/remotes.py` erzeugt daraus je Remote `remote-wz-<name>.yaml` und `remote-wz-<name>-halink.yaml`
  (Liste in `tools/remotes.json`, IP-Secret `ip_remote_wz_<name>`). Bauen/Flashen: `tools/esphome_run.mjs`.
- `components/ha_tunnel`, `components/network` (Bluetooth-Schicht), `components/dock_direkt`, `dock_hid`,
  `bluetooth_connection` (Dock), `custom_components/esphomeremote_konfig/ble_link.py` (HA-Gegenstück der Bluetooth-Schicht),
  `home-assistant/packages/` (Pakete, wie im Einsatz), `docs/entwicklung/` (Architektur, Funkkonzept, Messungen).

## Lizenz

MIT, siehe [LICENSE](LICENSE). Fremd-Assets: [THIRD-PARTY.md](THIRD-PARTY.md).
