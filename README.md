# ESPHomeRemote

**Eine Fernbedienung für Fernseher, Streamer und dein ganzes Smart Home** – mit Touchdisplay,
Bluetooth und Sprachassistent, gebaut auf der *OMOTE / Open Remote Rev6*-Platine und komplett in
**ESPHome**. Tasten, Menü und Startseite stellst du bequem im Browser in **Home Assistant** ein,
ohne zu programmieren.

> Du brauchst keine Programmierkenntnisse. Wenn du Home Assistant schon benutzt, bist du in etwa
> 30 Minuten fertig.

---

## Was sie kann

- **Steuert Fernseher, Streamer und Tablets per Bluetooth** – bis zu 4 Geräte, Umschalten mit einem
  Fingertipp in der Leiste unten („Aktivitäten“ wie bei Harmony).
- **Jede Taste frei belegbar** – kurz, doppelt und lang drücken getrennt, je Aktivität anders.
  Alles im Browser einstellbar (Konfigurator in Home Assistant).
- **Eigenes Menü auf dem Display** – Seiten, Knöpfe, Licht-Regler, Rollos, Anzeigen, Kamerabilder
  im Raster frei anordnen. Auch die Startseite und Popups (z. B. „Es hat geklingelt“).
- **Home Assistant direkt auf der Fernbedienung** – Licht, Rollos, Szenen, Skripte, Musik.
- **Sprachassistent** – Google auf dem Fernseher oder Home Assistant Assist.
- **Lange Akkulaufzeit** – Tiefschlaf mit etwa 1 mA, wacht auf beim Hochheben oder Tastendruck.
- **Kamera und Klingel** – Bild auf der Fernbedienung, Kamera auf dem Fernseher (in drei Größen).

---

## Was du brauchst

- Die Fernbedienung: **OMOTE / Open Remote, Platine Rev6** (ESP32-S3).
  Hardware: https://github.com/OMOTE-Community/OMOTE-Hardware
- Ein **USB-C-Kabel** und einen PC mit **Chrome oder Edge** (zum ersten Aufspielen).
- Für alle Funktionen: **Home Assistant** mit dem **ESPHome**-Add-on (oder „ESPHome Device Builder“).

---

## Schnellstart

### Nur ausprobieren (ohne Home Assistant)

1. Öffne **https://aggro600.github.io/ESPHomeRemote/** in Chrome oder Edge.
2. Fernbedienung per USB anstecken, **„Connect & Install“** klicken, Port wählen.
3. Nach dem Aufspielen fragt der Browser nach deinem **WLAN** – fertig.

Für Tastenbelegung, Menü und Smart Home brauchst du den Weg mit Home Assistant.

### Mit Home Assistant (empfohlen)

**Schritt 1 – ESPHome bereitlegen**
In Home Assistant: *Einstellungen → Add-ons → Add-on-Store →* **ESPHome Device Builder** installieren
und starten.

**Schritt 2 – Passwörter eintragen**
Im ESPHome-Add-on oben rechts auf **„Secrets“** klicken und die Zeilen aus
[`secrets.yaml.example`](secrets.yaml.example) ergänzen (WLAN steht meist schon drin).
Die Werte sind frei wählbar, merke dir nur `esphomeremote_konfig_key`.

**Schritt 3 – Fernbedienung anlegen**
1. Im ESPHome-Add-on **„+ Neues Gerät“** → **„Überspringen“** (bzw. „Leer anlegen“).
2. Den Inhalt von [`open-remote.yaml`](open-remote.yaml) in den Editor kopieren.
3. Oben anpassen:
   - `friendly_name`: Name deiner Fernbedienung (z. B. „Wohnzimmer“)
   - `ha_host`: die Adresse deines Home Assistant, z. B. `192.168.1.10:8123`
4. **Speichern** → **Installieren** → *„An diesen Computer anschließen“* → Fernbedienung per USB
   wählen. Das erste Bauen dauert etwa 10 Minuten.

**Schritt 4 – In Home Assistant übernehmen**
Nach dem Neustart meldet Home Assistant *„Neues Gerät gefunden“* (*Einstellungen → Geräte*).
**Hinzufügen** klicken. Den Schlüssel vergibt Home Assistant dabei selbst.
Dann beim Gerät auf **„Konfigurieren“** und **„Gerät darf Home-Assistant-Aktionen ausführen“**
einschalten – sonst kann die Fernbedienung kein Licht schalten.

**Schritt 5 – Konfigurator installieren (Tasten, Menü, Startseite)**
1. Den Ordner [`home-assistant/custom_components/esphomeremote_konfig`](home-assistant/custom_components/esphomeremote_konfig)
   nach `/config/custom_components/` kopieren (z. B. mit dem *File editor* oder *Samba*).
2. Die Datei [`home-assistant/packages/esphomeremote_konfig.yaml`](home-assistant/packages/esphomeremote_konfig.yaml)
   nach `/config/packages/` kopieren und dort deinen `device_name` eintragen (Standard: `open-remote`).
   Falls du noch keinen `packages`-Ordner nutzt, in `configuration.yaml` ergänzen:
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
3. In die `secrets.yaml` von **Home Assistant** (nicht ESPHome) dieselbe Zeile eintragen:
   ```yaml
   esphomeremote_konfig_key: "IRGENDEIN_LANGER_ZUFALLSTEXT"
   ```
4. Home Assistant neu starten. In der Seitenleiste erscheint **„Fernbedienung“**.

---

## Der Konfigurator

In der Seitenleiste **„Fernbedienung“** (am Handy und am PC):

| Bereich | Was du dort machst |
|---|---|
| **Tasten** | Jede Taste je Aktivität belegen: kurz / doppelt / lang. Bluetooth-Befehl, Home-Assistant-Aktion (aus einer Liste wählen), Infrarot oder Funktion der Fernbedienung. Kopieren und Einfügen wie gewohnt. |
| **Aktivitäten** | Beliebig viele anlegen (z. B. TV, Streamer, Tablet), bis zu 4 erscheinen unten in der Leiste. |
| **Startseite** | Bereiche (Player, Kameras, Menü-Knopf, Leiste) per Ziehen verschieben und in der Größe ändern. |
| **Menü** | Seiten und Unterseiten im Raster gestalten: Knöpfe, Schalter, Licht mit Regler, Rollos, Anzeigen, Kamerabild, alle Einstellungen der Fernbedienung. |
| **Popups** | Seiten, die von selbst aufgehen, z. B. wenn ein Skript startet oder es klingelt. |

**Speichern** schickt alles an die Fernbedienung – sie holt es sich beim nächsten Aufwachen ab
(Fortschritt wird angezeigt). Über die Pfeile oben rechts machst du eine **Sicherung** (Download)
und spielst sie auf einem anderen System wieder ein.

---

## Bedienung in Kürze

- **Menü-Taste**: Menü öffnen / schließen. Doppelt: Display aus.
- **Steuerkreuz + OK**: im Menü bewegen, auf der Startseite steuert es das aktive Gerät.
- **Zurück**: eine Seite zurück, in Popups schließen.
- **Leiste unten auf der Startseite**: zwischen Fernseher, Streamer usw. umschalten.
- **Bluetooth koppeln**: Menü → Einstellungen → Bluetooth → Platz wählen → „Neu koppeln“,
  dann am Fernseher / Streamer nach „Open Remote“ suchen.

---

## Häufige Fragen

**Die Fernbedienung schaltet in Home Assistant nichts.**
Beim ESPHome-Gerät „Gerät darf Home-Assistant-Aktionen ausführen“ einschalten (Schritt 4).

**Im Konfigurator steht „noch nicht übertragen“.**
Die Fernbedienung schläft. Einmal eine Taste drücken – sie holt die Belegung dann ab.

**Mehrere Fernbedienungen?**
Für jede in ESPHome ein eigenes Gerät mit eigenem `device_name`/`ha_name` anlegen (Schritt 3)
und alle Namen in `packages/esphomeremote_konfig.yaml` unter `devices:` eintragen.

**Seiten zeigen „nicht verfügbar“.**
Einige eingebaute Seiten (Startseite-Player, Klingel, Sleeptimer) nutzen die Entitäten aus
`open-remote.yaml` → `substitutions`. Trage dort deine ein oder lass sie weg – das Menü baust du
ohnehin im Konfigurator.

**Kamera auf dem Fernseher in Klein / Mittel / Groß** (optional):
Beispiel in `home-assistant/packages/beispiel_kamera_tv.yaml` und die Blaupause in
`home-assistant/blueprints/script/esphomeremote/`.

---

## Für Entwickler

- `open-remote-firmware.yaml` wird aus der Entwickler-Fassung erzeugt (`tools/veroeffentlichen.py`
  im Entwickler-Setup) – Änderungen bitte dort, nicht hier.
- Eigene Komponenten in `components/`: Display (`ili9341_i80`), Tastenfeld (`tca8418`), Akku
  (`max17048`), Lagesensor (`lis3dh`), Bluetooth-HID (`espidf_ble_keyboard`, `atv_voice`),
  Konfigurator (`tasten_konfig`, `menue`), SD-Karte, Touch (`ft63x6`).
- `home-assistant/custom_components/esphomeremote_konfig`: Integration mit Panel
  (`frontend/konfigurator.js`), liefert die Belegung per HTTP an die Fernbedienung.

## Lizenz

MIT, siehe [LICENSE](LICENSE). Fremd-Assets: [THIRD-PARTY.md](THIRD-PARTY.md).
