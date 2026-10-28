# Open Remote — erste Inbetriebnahme auf echter Hardware

Stand 2026-09-02. Die Rev6‑Hardware ist da. Nicht die komplette `esphomeremote.yaml`
auf einmal flashen — modulweise, damit ein Fehler eindeutig einem Baustein
zugeordnet werden kann.

## Voraussetzung: einmalig per USB flashen

Der erste Flash geht **nur über USB** (danach OTA über WLAN). ESP32‑S3 der Rev6:
USB‑C direkt am Modul (native USB‑Serial‑JTAG an GPIO19/20 — dieselben Pins wie
I2C, unkritisch, siehe Projektstatus).

Im ESPHome‑Dashboard: die jeweilige `esphomeremote-test-N-*.yaml` öffnen →
„Install" → „Plug into this computer" (bzw. `esphome run <datei> --device /dev/ttyACM0`).

Alle Test‑Configs nutzen dieselben WLAN‑/OTA‑Secrets wie deine anderen Geräte
(`!secret wifi_ssid` etc.). Jede hat einen eigenen `name:` (`esphomeremote-t1` …
`esphomeremote-t4`), taucht also als eigenes Gerät in HA auf. Nach Abschluss der
Tests die vier Test‑Geräte in HA wieder löschen.

## Board

Alle Configs: `board: esp32-s3-devkitc1-n16r8` (= N16R8: 16 MB Flash, 8 MB
OPI‑PSRAM — die Bestückung der Rev5/Rev6). `esp32:` ist bei der S3 die richtige
Plattform, `variant: ESP32S3` leitet ESPHome daraus ab.

---

## Schritt 1 — Display + Touch  (`esphomeremote-test-1-display.yaml`)

Grösstes Risiko: der `ili9341_i80`‑Treiber (8‑Bit‑Parallel‑Bus) ist der einzige
Baustein, der sich nicht per Codegen prüfen lässt.

**Prüfen:**
- Backlight geht an, „Open Remote / Display Test" + laufende Uhr sichtbar
- Button „Backlight" schaltet die Beleuchtung um
- Screen antippen → Log `touch bei X,Y` mit plausiblen Koordinaten (0‑239 / 0‑319)

**Wenn schwarz / verzerrt / Farben falsch:** am `display:`‑Block
`invert_colors: true`, dann `mirror_x` / `mirror_y` einzeln probieren. Nicht an
LVGL drehen. Wenn gar kein Bild: `enable_pin` (GPIO38, `inverted: true`) und die
8 Datenpins gegen die Netzliste prüfen.

`esphome compile` dieser Config lief bereits durch (RAM 33 %, Flash 14 %).

---

## Schritt 2 — Tasten + Akku + Bewegung  (`esphomeremote-test-2-keypad.yaml`)

**Prüfen:**
- I2C‑Scan im Log findet `0x19` (LIS3DH), `0x34` (TCA8418), `0x36` (MAX17048)
- Alle 23 Matrix‑Tasten als eigene `binary_sensor` in HA, Namen (S22 Play, S9 OK,
  …) gegen die Gehäusebeschriftung abgleichen. Stimmt eine nicht: Tabelle in
  `esphomeremote-projektstatus.md` „Vollständige Tasten‑Zuordnung"
- Power‑Taster: kurz → Log „Power kurz"; 7 s halten → Log „Power 7s → Neustart"
  + Reboot
- „Akkuspannung" 3,2–4,2 V, „Akkustand" plausibel
- Gerät bewegen → `binary_sensor` „Bewegung" geht kurz an

Der `on_key`‑Catch‑all loggt **jeden** Keycode — auch nicht zugeordnete. Damit
lässt sich eine falsch verdrahtete Taste direkt ablesen (`keycode=… row=… col=…`).

---

## Schritt 3 — IR  (`esphomeremote-test-3-ir.yaml`)

**Prüfen:**
- Fremd‑Fernbedienung auf GPIO4 richten, Taste → Log `Received NEC:` o. ä.
- HA‑Button „IR Test NEC senden" → an der IR‑LED (Handykamera) kurzes Blitzen
- „IR lernen Slot 0" → Taste an Fremd‑FB → Log „gespeichert" → „IR Slot 0
  abspielen" sendet es erneut

**Wenn nichts sendet / LED dauerhaft an:** `inverted: true` am
`remote_transmitter`‑Pin (GPIO5, Transistorstufe, active‑low) ist der Verdacht.

---

## Schritt 4 — BLE‑HID + Sprachsuche  (`esphomeremote-test-4-ble-voice.yaml`)

Stellt auf `espidf_ble_keyboard` + `atv_voice` um (wie wz-ir).

**Prüfen:**
- HA‑Button „Pairing Slot 0" → am Google TV / Chromecast unter
  *Einstellungen → Fernbedienungen & Zubehör* „OpenRemote" koppeln
- `binary_sensor` „Mit TV verbunden" geht an
- „D‑Pad Runter" / „D‑Pad OK" bewegen den Fokus am TV
- „Sprachsuche 4s" → TV öffnet das Assistant‑Overlay, ~4 s sprechen
  („Wetter in Berlin") → Erkennung erscheint
- Log: `ATVV format: ADPCM 16kHz …` und am Ende `Voice query finished (N frames)`

**Wenn Overlay aufgeht, aber keine Erkennung:** MTU / Intervall im Log prüfen
(`wz-ir HANDOVER.md`, „Durchsatz: gelöst"). Am TV nach jeder Änderung entkoppeln
und neu koppeln — der Codec wird nur beim Koppeln ausgehandelt.

**Noch nicht auf Firmware‑3.56‑Niveau** — die restlichen Angleichungen (160‑B/
20‑ms‑Framing, CAPS_RESP‑Layout, Fallback) stehen in `esphomeremote-3.56-delta.md`
und kommen nach einem ersten erfolgreichen 16‑kHz‑Test dran.

---

## Schritt 5 — alles zusammen  (`esphomeremote.yaml`)

Erst flashen, wenn 1–4 einzeln laufen. Enthält zusätzlich die LVGL‑UI mit den
HA‑Karten (Media‑Player, Licht, Schalter, Klingel‑Seiten). Vorher anpassen:
- Entity‑IDs (`media_player.wohnzimmer`, `light.wohnzimmer`, `switch.ventilator`,
  `lock.haustur`, `input_boolean.*` …) auf die echten HA‑Entities
- HA‑URL `homeassistant.local:8123` falls abweichend
- Tastenbelegung: die 23 Tasten sind noch nicht auf HID‑/IR‑/HA‑Aktionen
  gelegt — im `tca8418: on_key:` bzw. per `binary_sensor … on_press` ergänzen.
  Sprachsuche an eine Taste: `execute_action("atv_mic_start")` on press,
  `execute_action("atv_mic_stop")` on release.

## Bekannte offene Punkte

- `wifi: power_save_mode: none` (für BLE/Voice‑Koexistenz nötig) statt vorher
  `HIGH`. Etwas mehr Stromverbrauch; das Display‑Idle‑Timeout gleicht das
  teilweise aus.
- `espidf_ble_keyboard` bringt ein grosses Web‑Control mit — hier per
  `web_control: false` aus (open‑remote hat die LVGL‑UI).
- Kein physischer „Voice"‑Taster in der 23‑Tasten‑Belegung. Finale Zuordnung
  offen (Vorschlag: langer Druck auf OK, oder eine der Farbtasten).
