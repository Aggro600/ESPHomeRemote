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
- Für alle Funktionen: **Home Assistant** mit dem **ESPHome**-Add-on (oder „ESPHome Device Builder“)
  und **HACS** (für den Konfigurator).

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

**Schritt 2 – Konfigurator installieren (über HACS)**
1. Falls noch nicht da: **HACS** installieren (https://hacs.xyz – einmalig, 5 Minuten).
2. In Home Assistant **HACS** öffnen → oben rechts **⋮** → **„Benutzerdefinierte Repositories“**.
3. Einfügen: `https://github.com/Aggro600/ESPHomeRemote` – Typ **„Integration“** → **Hinzufügen**.
4. In HACS nach **„ESPHomeRemote“** suchen → **Herunterladen**.
5. Home Assistant **neu starten** (*Einstellungen → System → oben rechts ⏻ → Neu starten*).
6. *Einstellungen → Geräte & Dienste →* **„+ Integration hinzufügen“** → **„ESPHomeRemote“**.
   Den Namen der Fernbedienung so lassen (`open-remote`) → **Absenden**.
7. Es erscheint eine Zeile wie `esphomeremote_konfig_key: "…"` – **kopieren**, die brauchst du gleich.

**Schritt 3 – Passwörter eintragen**
Im ESPHome-Add-on oben rechts auf **„Secrets“** klicken und diese Zeilen ergänzen
(WLAN steht meist schon drin, die Passwörter sind frei wählbar):
```yaml
ota_pw: "ein-passwort"
ap_password: "noch-ein-passwort"
esphomeremote_konfig_key: "…"      # die kopierte Zeile aus Schritt 2
```

**Schritt 4 – Fernbedienung aufspielen**
1. Im ESPHome-Add-on **„+ Neues Gerät“** → **„Überspringen“** (bzw. „Leer anlegen“).
2. Den Inhalt von [`open-remote.yaml`](open-remote.yaml) in den Editor kopieren.
3. Nur **eine Zeile** anpassen: `ha_host` = Adresse deines Home Assistant, z. B. `192.168.1.10:8123`
   (steht unter *Einstellungen → System → Netzwerk*).
4. **Speichern** → **Installieren** → *„An diesen Computer anschließen“* → Fernbedienung per USB-C
   anstecken und auswählen. Das erste Mal dauert es etwa 10 Minuten.

**Schritt 5 – In Home Assistant übernehmen**
1. Home Assistant meldet *„Neues Gerät gefunden“* (*Einstellungen → Geräte & Dienste*) → **Hinzufügen**.
2. Beim Gerät auf **„Konfigurieren“** → **„Gerät darf Home-Assistant-Aktionen ausführen“** einschalten.
3. Fertig! Links in der Seitenleiste ist jetzt **„Fernbedienung“** – dort stellst du alles ein.

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
- `custom_components/esphomeremote_konfig`: Integration mit Panel
  (`frontend/konfigurator.js`), liefert die Belegung per HTTP an die Fernbedienung.

## Lizenz

MIT, siehe [LICENSE](LICENSE). Fremd-Assets: [THIRD-PARTY.md](THIRD-PARTY.md).
