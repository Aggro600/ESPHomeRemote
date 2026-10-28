# Handoff-Briefing für Claude Code

Dieses Projekt wurde in einer Konversation mit Claude (claude.ai) entwickelt
und geplant. Diese Datei ist das Briefing für dich (Claude Code), damit du
ohne Rückfragen direkt weiterarbeiten kannst. Lies zuerst diese Datei
komplett, dann bei Bedarf `OpenRemote_zu_ESPHome_Projektstatus.md` für alle
Details, Hintergründe und Begründungen.

## ⚠️ STAND 2026-09-02 zuerst lesen

Die Hardware ist da. Seither:
- **`esphomeremote-projektstatus.md`**, Abschnitt „UPDATE 2026-09-02" — BLE/Voice
  von `ble_hid_atv` auf `espidf_ble_keyboard` + `atv_voice` umgestellt.
- **`esphomeremote-flash-reihenfolge.md`** — gestufte Inbetriebnahme, 4 Test-
  Configs (`esphomeremote-test-1..4-*.yaml`), alle `esphome compile`-gebaut.
- **`esphomeremote-3.56-delta.md`** — was `atv_voice` noch fehlt, um mit der
  aktuellen OpenRemote-Firmware (3.56, 16 kHz) gleichzuziehen. Erst nach
  erstem Hardware-Test angehen.

Vieles unten (v. a. „BLE-HID + Android-TV-Sprachsuche (ATVV) — komplett
portiert" und die `ble_hid_atv`-Erwähnungen) ist damit **überholt**.

## Was das Projekt ist

Eine OpenRemote Rev6 Hardware (Fork von OMOTE, ESP32-S3-basierte Universal-
fernbedienung mit Touchscreen) soll komplett mit eigener ESPHome-Firmware
betrieben werden statt der originalen Arduino-Firmware. Ziel: Tasten,
IR-Fernbedienung (inkl. Raum-Blaster in anderen Zimmern), Bluetooth-HID +
Android-TV-Sprachsuche fürs Fernsehen, und volle Home-Assistant-Integration
über die native ESPHome-API.

Die Hardware ist zum Zeitpunkt dieses Handoffs **noch nicht eingetroffen**
(laut Nutzer "paar Tage" entfernt). Bis dahin ist alles reine Software-
vorbereitung, die gegen `esphome config`/`esphome compile` (Schema +
C++-Codegenerierung) validiert wurde, aber **niemals echt kompiliert oder
auf Hardware getestet** werden konnte (siehe Einschränkungen unten).

## Ordnerstruktur in diesem Paket

```
OpenRemote-ESPHome/
├── CLAUDE.md                              <- diese Datei
├── OpenRemote_zu_ESPHome_Projektstatus.md <- Vollständige Projektdokumentation
├── esphome_components/                    <- alle selbstgeschriebenen Components
│   ├── tca8418/              Tastenmatrix-Treiber (5x5-Matrix + Power-GPI)
│   ├── max17048/              Akku-Fuel-Gauge-Sensor
│   ├── ble_hid_atv/           BLE-HID-Tastatur + Android-TV-Sprachsuche (ATVV)
│   ├── ili9341_i80/           Display-Treiber (8-Bit-Parallel-Bus, ESP-IDF esp_lcd)
│   ├── example-keypad-and-battery.yaml
│   ├── example-ble-hid-atv.yaml
│   ├── room-ir-blaster-template.yaml
│   └── openremote-base-display-lvgl.yaml  <- Display+Touch+LVGL-UI-Grundgerüst
└── OMOTE_Rev5_CAD_fallback/   Gehäuse-STLs als Zwischenlösung (siehe unten)
```

## Sofort zu erledigen, bevor irgendetwas anderes

**`esphome compile` mit echtem Internetzugriff einmal laufen lassen**, für
jede der vier Component-YAMLs (oder eine kombinierte Test-YAML). In der
Umgebung, in der dieses Projekt entstand, war der Download der ESP-IDF-
Toolchain von `dl.espressif.com` netzwerkseitig blockiert - deshalb konnte
nur bis "Generating C++ source..." (Codegen-Ebene) getestet werden, nie der
eigentliche g++-Build. Auf deiner Maschine sollte das funktionieren. Das ist
der wichtigste offene Schritt, um echte C++-Syntaxfehler zu finden, die
reine Codegen-Validierung nicht immer fängt.

```bash
pip install esphome
cd esphome_components
# Minimal-Testkonfiguration pro Component bauen (siehe die example-*.yaml
# Dateien als Ausgangspunkt) und dann:
esphome compile <config>.yaml
```

## Bekannte, bereits gefundene Probleme / wichtige Fakten

Diese wurden in der ursprünglichen Konversation durch Analyse der Referenz-
Firmware (`OpenRemote_1.0.ino`, im Repo `LORDSn1per/OpenRemote-Firmware`)
und der Rev6-KiCad-Netzliste (`LORDSn1per/OpenRemote-Hardware`) verifiziert,
nicht geraten. Details und Begründungen stehen im Projektstatus-Dokument.

1. **Tastenmatrix ist KEINE klassische Matrix**, sondern 5x5-Matrix (23
   Tasten) + 1 Einzel-GPI-Pin für den Power-Taster (ROW5). Die `tca8418`-
   Component deckt das ab (`extra_gpi_pins: [5]`).

2. **MAX17048 ist NICHT kompatibel** mit ESPHomes eingebauter
   `max17043`-Component (andere Register-Skalierung, Faktor-16-Fehler bei
   falscher Verwendung). Eigene `max17048`-Component nutzen.

3. **ESPHome hat KEINEN nativen Treiber für den 8-Bit-Parallel-Bus (8080/
   "i80")**, den das Display nutzt. Der eingebaute `ili9xxx` ist SPI-only.
   Die `ili9341_i80`-Component wurde dafür neu geschrieben (ESP-IDF
   `esp_lcd_panel_io_i80`-API, wiederverwendet aber ESPHomes eigene, geprüfte
   ILI9341-Init-Kommandotabelle). **Dies ist der Baustein mit dem höchsten
   Risiko** - Display-Timing/Farbformat lässt sich nur an echter Hardware
   verifizieren, nicht durch Codegen allein.

4. **BLE braucht `framework: esp-idf`** (nicht arduino) und nutzt bewusst
   ESPHomes eigenen `esp32_ble_server`-Stack statt einer zweiten,
   konkurrierenden BLE-Stack-Initialisierung.

5. **IR-Sender-Pin (GPIO5) ist active-low** (Transistor-Treiberstufe) - beim
   `remote_transmitter` `inverted: true` setzen. Dieser Fix ist in
   `openremote-base-display-lvgl.yaml` als Kommentar vermerkt, aber noch
   NICHT in `example-ble-hid-atv.yaml`/anderen IR-Beispielen nachgetragen -
   das solltest du als einen der ersten Schritte erledigen.

6. **Alle Pins** (I2C SDA=GPIO20/SCL=GPIO19, Display, Mikrofon, IR, Touch)
   sind gegen die Referenz-Firmware und die Rev6-Netzliste verifiziert -
   Tabelle im Projektstatus-Dokument, Abschnitt "Display: kritischer Fund".

## Wichtige Entscheidungen, die schon getroffen wurden (nicht neu diskutieren)

- Eigene Components statt Community-Components (waren lückenhaft/fehlerhaft,
  siehe Analyse im Projektstatus-Dokument)
- Home-Assistant-Anbindung über native ESPHome-API, kein MQTT
- IR-Architektur: Remote sendet lokal UND kann über
  `homeassistant.service:` an separate Raum-Blaster (auch ESPHome, Vorlage
  in `room-ir-blaster-template.yaml`) weiterleiten
- BLE: volle ATVV-Sprachsuche inkl. echtem Audio-Streaming (I2S-Mikrofon ->
  IMA-ADPCM -> BLE-Notify), nicht nur reines HID

## Was noch offen ist (aus dem Projektstatus-Dokument)

- **Nur über Discord/Community lösbar** (kein Zugriff für die KI): Rev6-
  spezifische Gehäuse-STLs (BuyDisplay-Variante), exakter Schrauben-Kopftyp.
  Zwischenlösung: `OMOTE_Rev5_CAD_fallback/` enthält die Original-OMOTE-
  Rev5-Gehäusedateien (passen zur alten Adafruit-Display-Variante, nicht zur
  neuen BuyDisplay-Variante, aber besser als nichts).
- Entity-IDs in `openremote-base-display-lvgl.yaml` sind Platzhalter
  (`media_player.wohnzimmer` etc.) - auf echte Home-Assistant-Instanz
  anpassen.
- Kein physischer "Voice"-Taster in der 23-Tasten-Belegung vorgesehen -
  finale Zuordnung noch zu klären.
- LVGL-Karten-Layout ist ein erster Entwurf, kein finales Design.

## Empfohlene Reihenfolge, sobald die Hardware da ist

1. Kleinster isolierter Test zuerst: reiner Display+Touch-Test (`ili9341_i80`
   + `ft63x6`), unabhängig von allem anderen - höchstes Risiko, früh
   verifizieren
2. Tastenmatrix (`tca8418`) gegen die Tasten-Zuordnungstabelle im
   Projektstatus-Dokument testen
3. IR (lokal + ein Raum-Blaster zum Testen der HA-Weiterleitung)
4. BLE-HID + Sprachsuche (Pairing, dann Tasten, dann Audio-Streaming per
   BLE-Sniff verifizieren)
5. LVGL-UI mit echten Entity-IDs verbinden

## Ton/Arbeitsweise

Der Nutzer möchte, dass eigenständig weitergearbeitet wird, ohne bei jedem
Schritt nachzufragen - nur bei echten Grundsatzentscheidungen (z. B. "soll
ich Ansatz A oder B verfolgen, beide sind aufwändig") sollte gefragt werden.
Gefundene Probleme sollen klar benannt und dann direkt gelöst werden, nicht
nur gemeldet.
