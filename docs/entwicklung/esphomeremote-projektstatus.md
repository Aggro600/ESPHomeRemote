# OpenRemote → ESPHome — Projektstatus

**Ziel:** Die OpenRemote Rev6 Hardware (Fork von OMOTE) mit eigener ESPHome-Firmware
betreiben statt der originalen OpenRemote-Firmware. Fokus: Tasten belegen, TV per
Bluetooth steuern (inkl. Android-TV-Sprachsuche), Home Assistant direkt einbinden
(Lichter/Schalter/Media-Player mit Cover-Art, native API statt MQTT).

Repos:
- Hardware: https://github.com/LORDSn1per/OpenRemote-Hardware (Rev6, PCB/Gerber/BOM/CPL)
- Firmware (Referenz, NICHT die Zielfirmware): https://github.com/LORDSn1per/OpenRemote-Firmware
- Ursprungsprojekt: https://github.com/OMOTE-Community

---

## Hardware-Bausteine (Rev6)

| Baustein | Chip | ESPHome-Status |
|---|---|---|
| Display | ILI9341 | ✅ nativ (`ili9xxx`) |
| Touch | FT6206 | ✅ nativ (`ft63x6`) |
| Beschleunigungssensor | LIS3DHTR | ✅ nativ (`lis3dh`) |
| Mikrofon | I2S (INMP441-kompatibel, MS3625) | ✅ nativ (I2S-Mikrofon) |
| IR TX/RX | — | ✅ nativ (`remote_transmitter`/`remote_receiver`) |
| Tastenmatrix | TCA8418 (26 Taster S1–S26) | ⚠️ External Component nötig |
| Akku-Fuel-Gauge | MAX17048 | ❌ ESPHome `max17043`-Komponente NICHT direkt kompatibel (siehe unten) |
| BLE-Keyboard + Android-TV-Sprachsuche | — | ⚠️ Quellcode zum Portieren vorhanden (siehe unten) |
| WLAN + Home Assistant | — | ✅ nativ über ESPHome-API (kein MQTT nötig) |

---

## Bestellstatus (Hardware-Teile)

- ✅ Platine (JLCPCB, 10 Stück, BOM/CPL-Fehler behoben)
- ✅ Display (BuyDisplay ER-TFT028A2-4, kapazitiv, ohne Connector, 10 Stück)
- ✅ Akku (103450, 3.7V 2000mAh, JST-PH 2.0mm)
- ✅ Mikrofon (INMP441-kompatibel)
- ⚠️ **Gehäuse (3D-Druck-STLs) — Workaround gefunden, Rev6-spezifische Dateien
  weiterhin fehlend.** Rev6-Dateien (`Cover Plate v19 - BuyDisplay LCD.stl`,
  Bambu-Lab-3MF) sind laut README fertig, aber nirgends veröffentlicht (kein
  CAD-Ordner im Fork-Repo, keine GitHub Releases) — Discord-Nachfrage bleibt nötig
  für die *optimale* Lösung.
  **Aber:** Das Original-Repo `OMOTE-Community/OMOTE-Hardware` (Rev5) hat einen
  vollständigen `CAD`-Ordner mit `Case.stl`, `Cover Plate.stl`, `Button Grid.stl`,
  `Off Button.stl`, `Color Button.stl` — plus Fusion-360-Quelldatei (`OMOTE.f3z`)
  und STEP-Export (`OMOTE v25.step`) für eigene Anpassungen. Da Rev6 laut Vergleich
  oben elektrisch/mechanisch nur Display, Mikrofon, Ladeschaltung und Schrauben-
  anzahl geändert hat (Taster-Layout identisch), sollten diese Rev5-Dateien mit der
  originalen Adafruit-Display-Variante (nicht der neuen BuyDisplay-Variante)
  passen. Dateien liegen bereit unter `OMOTE_Rev5_CAD_fallback/` im Output.
  Einschränkung: passt zur alten Schraubenanzahl/-position von Rev5, nicht zu den
  fünf rückseitigen M3×8mm-Schrauben von Rev6 — vor dem Druck gegenprüfen bzw. als
  Zwischenlösung nutzen, bis die echten Rev6-Dateien auftauchen.
- ⚠️ Schrauben (5× M3×8mm) — Kopftyp (Senkkopf vs. Flachkopf) nicht offiziell
  dokumentiert, auch nicht in Rev5-Unterlagen auffindbar (Schrauben tauchen nicht
  in der Elektronik-BOM auf). Empfehlung bleibt: Senkkopf/Kreuzschlitz, gängigster
  Standard für 3D-gedruckte Gehäuse. Letzte Sicherheit nur über Discord-Nachfrage
  oder Nachmessen der STEP-Datei möglich.

---

## Für die Tastenmatrix (TCA8418) — vollständig analysiert

**Korrektur:** Es sind **25 Taster** (S1–S26, S21 existiert nicht), nicht 26.
Davon sind **23 echte Matrix-Tasten** + 1 Power-Taster (S17, Sonderfall) +
1 ESP32-Reset-Taster (S26, hängt gar nicht am TCA8418).

### Zwei Community-Components geprüft und Quellcode analysiert

1. **evil-dog/esphome-tca8418** — https://github.com/evil-dog/esphome-tca8418
   Nutzt den TCA8418 NUR im GPI-Modus (jeder der 18 Pins = ein einzelner
   GPIO-Eingang mit Pullup, kein Matrix-Scanning). Erklärt die 18-Tasten-Grenze:
   architektonisch bedingt, nicht nur unvollständig — mit dieser Betriebsart
   sind >18 Taster unmöglich. Für uns als alleinige Basis ungeeignet.

2. **csaboka21/esphome-TCA8418** — https://github.com/csaboka21/esphome-TCA8418
   (gefunden über eine Diskussion in `esphome/feature-requests#3601`, Juni 2026)
   Nutzt echtes Matrix-Scanning (`KP_GPIO1-3`-Register), bis zu 80 Tasten möglich.
   **Bug gefunden:** Schema erlaubt `columns: 1..14`, der TCA8418 hat aber nur
   10 Spalten-Pins (C0–C9) — bei >10 Spalten schreibt der Code eine falsche
   Bitmaske in `KP_GPIO3` (nur 2 gültige Bits). Muss vor Nutzung auf max. 10
   gecappt werden. Kein GPI-Support für Nicht-Matrix-Pins (siehe unten, wird
   für unseren Power-Taster gebraucht). Hinweis im Repo: "created with the
   help of AI", nur 1 Star, ungetestet über den Cardputer-Anwendungsfall hinaus.

### Tatsächliche Verdrahtung auf der Platine (aus Rev6-KiCad-Schaltplan/PCB extrahiert)

Keine klassische 8×10-Matrix, sondern **5×5-Matrix + zusätzliche Einzel-GPI-Pins**
auf denselben Chip:

| TCA8418-Pin | Netz | Funktion |
|---|---|---|
| ROW0–ROW4 | SW_E, SW_D, SW_C, SW_B, SW_A | echte Matrix-Zeilen (5) |
| COL0–COL4 | SW_1–SW_5 | echte Matrix-Spalten (5) |
| ROW5 | SW_PWR | Power-Taster (S17), als **Einzel-GPI**, nicht Teil der Matrix |
| ROW6 | SD_DET | SD-Karten-Erkennung (nicht in Firmware über TCA8418 ausgewertet — separater ESP32-Pin) |
| ROW7 | – | unbenutzt |
| COL5 | USB_3V3 | unbenutzt in aktueller Firmware trotz Verdrahtung |
| COL6–COL9 | – | unbenutzt |

S26 (ESP32-Reset/Boot-Taster) hängt gar nicht am TCA8418, sondern direkt an
`ESP_EN`+GND — braucht in ESPHome keine Software-Behandlung.

### Referenz-Firmware liefert fertige, verifizierte Lösung

`OpenRemote_1.0.ino` (im Firmware-Repo) enthält bereits eine **funktionierende,
getestete** TCA8418-Ansteuerung, die exakt zu unserer Hardware passt
(`PHYSICAL_BUTTON_COUNT = 23`, `ADDR_TCA8418 = 0x34`, `PIN_TCA_INT = GPIO8`).
Empfehlung: **direkt von hier portieren**, statt eine der beiden Community-
Components anzupassen — spart die Fehlersuche, die wir bei csaboka21 schon
hatten.

**Verifizierte Register-Init-Sequenz** (Kommentar im Code: "Rows 0-4 and
columns 0-4 form the 5x5 Rev 5 key matrix. S17 is separately wired to ROW5
as an active-low GPI"):

```
0x1D (KP_GPIO1)   = 0x1F   // Rows 0-4 als Matrix-Zeilen aktiv
0x1E (KP_GPIO2)   = 0x1F   // Cols 0-4 als Matrix-Spalten aktiv
0x1F (KP_GPIO3)   = 0x00   // Cols 8-9 ungenutzt
0x20 (GPI_EM1)    = 0x20   // Bit5 = ROW5 (Power-Taster) als Einzel-GPI mit Event
0x23              = 0x00
0x26              = 0x00
0x2C              = 0x00
0x1A (GPIO_INT_EN1) = 0x20 // Interrupt für ROW5-GPI aktivieren
0x01 (CFG)        = 0x03   // KE_IEN + GPI_IEN
```

Matrix-Tasten kommen als Keycode 1–80 (Formel: `row*10+col+1`) aus dem FIFO
(Register 0x04), der Power-Taster als **Event-Code 102** (GPI-Eventcodes
laufen bei 97 für Pin 0 los, ROW5 = Pin-Index 5 → 97+5=102 — genau das
Schema, das `evil-dog`s Component für alle 18 Pins verwendet).
Der Power-Taster wird in der Firmware speziell behandelt: kurzer Druck =
"alles aus", 7 Sekunden halten = Neustart.

### Vollständige Tasten-Zuordnung (S-Nummer → Keycode → Funktion)

Aus `PHYSICAL_BUTTON_NAMES[]` / `PHYSICAL_SWITCH_NUMBERS[]` im Firmware-Code,
verifiziert gegen die PCB-Netzliste:

| Switch | Row,Col | Keycode | Funktion |
|---|---|---|---|
| S25 | 0,4 | 5 | Stop |
| S24 | 0,3 | 4 | Rewind |
| S22 | 0,1 | 2 | Play |
| S16 | 1,0 | 11 | Forward |
| S23 | 0,2 | 3 | Menu |
| S6 | 3,0 | 31 | Info |
| S19 | 1,3 | 14 | D-pad Up |
| S10 | 3,4 | 35 | D-pad Down |
| S20 | 1,4 | 15 | D-pad Left |
| S7 | 3,1 | 32 | D-pad Right |
| S9 | 3,3 | 34 | OK |
| S18 | 1,2 | 13 | Back |
| S1 | 4,0 | 41 | Return |
| S8 | 3,2 | 33 | Volume Up |
| S3 | 4,2 | 43 | Volume Down |
| S4 | 4,3 | 44 | Mute |
| S2 | 4,1 | 42 | Channel Up |
| S12 | 2,1 | 22 | Channel Down |
| S5 | 4,4 | 45 | Record |
| S13 | 2,2 | 23 | Red |
| S15 | 2,4 | 25 | Green |
| S14 | 2,3 | 24 | Yellow |
| S11 | 2,0 | 21 | Blue |
| S17 | ROW5 (GPI) | 102 | Power (Sonderbehandlung) |

### Empfehlung für die ESPHome-Component

Eigene kleine Component bauen (Fork von csaboka21 als Ausgangspunkt möglich,
aber Kernlogik direkt aus obiger verifizierter Init-Sequenz übernehmen):
- Matrix-Scan 5×5 wie csaboka21 (Columns-Bug fixen/irrelevant, da wir nur 5 brauchen)
- Zusätzlich Einzel-GPI-Pfad für ROW5 (Power-Taster, Keycode 102) wie evil-dog's
  GPI-Ansatz — long-press/short-press-Logik dafür in ESPHome nachbauen
  (`on_press`/`on_release` mit Zeitmessung, oder `binary_sensor` mit
  `delayed_on`/`delayed_off` für die 7-Sekunden-Reboot-Erkennung)
- SD_DET/USB_3V3 (ROW6/COL5) vorerst ignorieren — auch die Referenzfirmware
  nutzt sie nicht über den TCA8418

---

## Für BLE-Keyboard + Android-TV-Sprachsuche

**Status: erledigt.** Siehe Abschnitt "BLE-HID + Android-TV-Sprachsuche (ATVV)
— komplett portiert, inkl. Audio" weiter unten für die fertige ESPHome-
Component (`ble_hid_atv`) und alle Details zur Portierung aus
`OpenRemote_1.0.ino`.

---

## Home-Assistant-Anbindung (Entscheidung: native API, kein MQTT)

Ursprünglich MQTT diskutiert (Broker vorhanden), dann auf native ESPHome-API
umgeschwenkt, weil einfacher für den konkreten Anwendungsfall:

- **Zustand HA → Remote** (Lichter, Media-Player mit Cover-Art): über
  `homeassistant.text_sensor` (zieht auch Attribute wie `entity_picture`,
  push-basiert über die bestehende ESPHome-API-Verbindung, kein Polling)
  + `online_image`-Komponente zum Nachladen des Covers bei Änderung
- **Befehle Remote → HA**: einfache `homeassistant.service`-Calls
  (z. B. `media_player.media_play`, `light.turn_on`)
- Kein Custom Component / keine eigene HA-Integration nötig — alles über
  ESPHomes eingebaute Home-Assistant-Aktionen lösbar

UI-Bausteine, die noch gebaut werden müssen (LVGL, `lvgl:`-Komponente in ESPHome):
- Media-Player-Karte: Cover-Art, Titel/Interpret, Play/Pause/Skip, Lautstärke
- Licht-Karte: Toggle, ggf. Helligkeits-Slider
- Schalter-Karte: einfacher Toggle
- Tastenbelegung: IR / BLE-HID / HA-Service, je nach Taste konfigurierbar

---

## Nächste Schritte (sobald Hardware da ist)

1. Grundgerüst: fertig konfiguriert (`ili9341_i80`-Display + `ft63x6`-Touch +
   Hintergrundbeleuchtung), gegen echte Hardware testen — größtes Risiko im
   ganzen Projekt, da als einzige Component nicht durch Codegen allein auf
   Korrektheit prüfbar (Timing/Farbformat)
2. Tastenmatrix: fertige `tca8418`-Component gegen die echte Hardware testen
3. IR: fertig konfiguriert (Remote-eigenes IR + Raum-Blaster-Template) —
   `inverted`-Handling für den active-low IR-Sender-Pin (GPIO5) noch in die
   Beispiel-YAML nachtragen, dann gegen echte Hardware testen
4. BLE-HID + Sprachsuche: fertige `ble_hid_atv`-Component gegen echte
   Hardware testen (Pairing, HID-Tasten, Audio-Streaming per BLE-Sniff
   verifizieren)
5. LVGL-UI (Media-Player/Licht/Schalter): fertig, Entity-IDs auf echte HA-
   Instanz anpassen, dann gegen echte Hardware testen

Empfehlung: mit dem kleinsten, isolierten Baustein anfangen (z. B. reiner
Display+Touch-Test), nicht alles auf einmal.

## MAX17048-Kompatibilität — geprüft, NICHT kompatibel

Gegen den Linux-Kernel-Treiber-Quellcode für die MAX1704x-Familie geprüft
(`power: supply: max17040`-Treiber, unterstützt alle 8 Chip-Varianten mit
dokumentierten Register-Unterschieden):

| | MAX17043 | MAX17048 |
|---|---|---|
| VCELL-Register | 12-Bit-Wert in oberen Bits, `>>4`-Shift nötig | voller 16-Bit-Wert, kein Shift |
| Skalierung | 1.25 mV/LSB | 78.125 µV/LSB (625/8) |
| Reset-Wert | 0x0054 | 0x5400 |

Die ESPHome-`max17043`-Komponente ist auf die MAX17043-Skalierung fest verdrahtet.
Direkt am MAX17048 verwendet, käme eine um Faktor ~16 falsche Spannungsanzeige
heraus (falscher Shift + falscher mV/LSB-Faktor). I2C-Adresse (0x36) stimmt zwar
überein, aber das reicht nicht. **Für den MAX17048 braucht es eine eigene, kleine
Sensor-Component** (oder einen `i2c`-Direktzugriff per `lambda` mit korrektem
Shift/Faktor) — kein Fall von "einfach die bestehende Komponente nehmen".

## Gehäuse-Fallback bereitgestellt

Im Output-Ordner `OMOTE_Rev5_CAD_fallback/` liegen die Original-OMOTE-Rev5-
Gehäusedateien (`Case.stl`, `Cover Plate.stl`, `Button Grid.stl`, `Off Button.stl`,
`Color Button.stl`, plus `OMOTE v25.step` als CAD-Quelle für eigene Anpassungen).
Passt zur originalen Adafruit-Display-Variante; bei Nutzung der neuen BuyDisplay-
Variante braucht es weiterhin die Rev6-spezifische `Cover Plate v19`.

## OMOTE-Rev5-vs-OpenRemote-Rev6-Vergleich

PCB-Schaltplan/Netzliste direkt verglichen: TCA8418-Verdrahtung, Tastenmatrix und
alle Netznamen sind zwischen Rev5 und Rev6 identisch (teils sogar gleiche
Bauteil-UUIDs im PCB-File — 1:1 aus Rev5 übernommen). Rev6-Änderungen laut
offiziellem README betreffen nur: neues BuyDisplay-Display, eingebautes
I2S-Mikrofon, schnelleres Laden (TP4056 ~1A), Vorbereitung für Pogo-Pin-Ladedock,
5 Gehäuseschrauben statt weniger, Branding/QR-Code.

Kein existierender OMOTE-ESPHome-Port gefunden (nur eine alte, nie umgesetzte
Diskussion mit der Idee dazu) — wir sind hier tatsächlich Vorreiter, keine
Doppelarbeit.

## Eigene ESPHome-Components gebaut

Zwei eigene External Components erstellt, im Output-Ordner `esphome_components/`:

**`tca8418`** — Matrix+GPI-Hybrid-Treiber, basierend auf der oben verifizierten
Register-Sequenz. Konfigurierbar über `rows`/`columns` (Standard 5×5 für unsere
Hardware) plus `extra_gpi_pins` (Liste roher TCA8418-Pin-Indizes 0–17 für
Taster außerhalb der Matrix, z. B. `[5]` für den Power-Taster auf ROW5).
Bietet `binary_sensor`-Plattform (pro Keycode) und einen `on_key`-Automation-
Trigger mit `keycode`, `row`, `col`, `pressed`, `long_press`.

**`max17048`** — eigener Fuel-Gauge-Sensor mit korrekter Skalierung
(78.125 µV/LSB, kein Shift), da die Standard-`max17043`-Komponente wie oben
beschrieben falsche Werte liefern würde. Bietet `voltage`- und
`battery_level`-Sensoren.

`esphome_components/example-keypad-and-battery.yaml` zeigt die komplette
Verwendung: alle 23 Matrix-Tasten mit echten Namen, Power-Taster-Sonderlogik
(kurz = alles aus, 7s halten = Neustart — direkt aus der Referenz-Firmware
übernommen), und die Akku-Sensoren.

**Validierung:** `esphome config` läuft fehlerfrei durch (Schema korrekt).
Die C++-Codegenerierung (`esphome compile`) läuft ebenfalls sauber durch und
erzeugt korrekt aufgelöste Klassen/Templates im generierten `main.cpp`. Der
eigentliche Cross-Compile-Schritt (g++ für Xtensa) konnte in dieser Umgebung
nicht zu Ende laufen, da der ESP-IDF-Python-Constraints-Download von
Espressif-Servern network-seitig blockiert ist — das ist eine reine
Sandbox-Einschränkung, kein Hinweis auf einen Fehler im Code. Vor dem ersten
Flash auf echte Hardware unbedingt einmal `esphome compile` in einer normalen
Umgebung mit vollem Internetzugriff laufen lassen.

## IR: eingebaut in die Remote + separate Raum-Blaster (beide ESPHome)

**IR-Pins der Remote verifiziert** (Rev6-PCB-Netzliste, direkt am
ESP32-S3-WROOM-1): `IR_LED` (Sender) = **GPIO5**, `IR_RX` (Empfänger) =
**GPIO4**. Beides läuft über ESPHomes native `remote_transmitter`/
`remote_receiver`-Components — keine externe Bibliothek nötig, deckt u. a.
NEC, Sony, RC5/RC6, Samsung, LG, Panasonic, Coolix, diverse Klimaanlagen-
Protokolle sowie Raw-Codes ab.

**Architektur für "Remote + IR-Blaster pro Raum":**

Ein ESPHome-Gerät kann nicht direkt die `api:actions:` eines anderen
ESPHome-Geräts aufrufen — das läuft immer über Home Assistant. HA stellt
jede `api:actions:`-Aktion eines Geräts automatisch als Service
`esphome.<device_name>_<action>` bereit. Damit ergibt sich pro Taste auf der
Remote die Wahl:

1. **Lokal senden** (`remote_transmitter.transmit_nec:` direkt in der
   Remote-Firmware) — für Geräte direkt vor der Remote.
2. **An einen Raum-Blaster weiterleiten** (`homeassistant.service:` ruft
   `esphome.<raum>_ir_blaster_send_nec` mit Adresse/Kommando als Parameter
   auf) — für Geräte in einem anderen Raum.

Beides lässt sich pro Taste frei kombinieren oder situativ umschalten (z. B.
über eine "aktiver Raum"-Auswahl in der LVGL-UI, die dann den Zielnamen für
die HA-Service-Aufrufe bestimmt).

**Zwei neue Dateien im Output:**

- `esphome_components/room-ir-blaster-template.yaml` — generische Vorlage
  für einen Raum-Blaster: eigene `api:actions:` (`send_nec`, `send_samsung`,
  `send_raw`), `remote_transmitter` + optionaler `remote_receiver` zum
  Anlernen unbekannter Fernbedienungscodes über den ESPHome-Log
  (`dump: all`). Pro Raum nur `device_name`/`friendly_name`/Pins über die
  `substitutions:` anpassen und neu flashen.
- `esphome_components/example-keypad-and-battery.yaml` erweitert um den
  IR-Teil der Remote selbst sowie ein konkretes Beispiel (Kanal-Hoch-Taste),
  das sowohl lokal sendet als auch an einen Wohnzimmer-Blaster weiterleitet.

**Beide Configs mit `esphome config` validiert — "Configuration is valid!"**
(inkl. der `api:actions:`-Parametrisierung mit `variables:` und
`remote_transmitter.transmit_raw`/`transmit_nec`/`transmit_samsung`).

**Hardware-Hinweis für eigene Raum-Blaster:** Eine IR-LED direkt an einem
GPIO liefert nur wenige mA und damit wenig Reichweite/Winkel. Für einen
brauchbaren Raum-Blaster braucht es einen Transistor/MOSFET als Treiber
(Standardschaltung, in den meisten ESPHome-IR-Projekten/-Tutorials zu
finden) oder fertige IR-Blaster-Boards mit mehreren LEDs.

## BLE-HID + Android-TV-Sprachsuche (ATVV) — komplett portiert, inkl. Audio

**Architekturentscheidung umgesetzt:** Component nutzt ESPHomes eigenen
BLE-Stack (`esp32_ble_server`, ESP-IDF/Bluedroid) statt der Arduino-
`BLEDevice`-API der Referenz-Firmware — kein Konflikt mit ESPHomes eigener
BLE-Verwaltung. Alle dafür nötigen API-Signaturen (`ESPBTUUID::from_uint16/
from_raw`, `BLEService::create_characteristic`, `BLECharacteristic::
add_descriptor`, `InternalGPIOPin::get_pin()` etc.) wurden gegen den
tatsächlich installierten ESPHome-2026.8.1-Quellcode geprüft, nicht geraten.
Erfordert `framework: esp-idf` (nicht arduino) in der Remote-Config.

**Vollständig portiert:**
- BLE-HID-Gerät (kombinierter 10-Byte-Report: Tastatur + Consumer-Control in
  einem Input-Report, exakt wie in der Referenz-Firmware, inkl. des dort
  dokumentierten Workarounds gegen doppelte gleich-UUID-Report-
  Characteristics)
- Device-Information-Service (Hersteller, PnP-ID)
- ATVV-Service (Capability-Handshake, MIC_OPEN/MIC_CLOSE-Steuerung,
  AUDIO_START/AUDIO_STOP/AUDIO_SYNC-Pakete)
- **Vollständige Audio-Pipeline**: I2S-Mikrofonaufnahme (16kHz, dezimiert auf
  8kHz) → klassischer IMA-ADPCM-Encoder → 20-Byte-Frames alle 5ms per
  BLE-Notify, in einem eigenen FreeRTOS-Task (Core 0), exakt wie im Original
- Mikrofon-Pins **gegen die Rev6-Netzliste verifiziert** (nicht nur aus der
  Referenz-Firmware übernommen): BCLK=GPIO15, WS=GPIO17, DATA=GPIO7,
  Power=GPIO45 — geteilt mit den (in unserem ESPHome-Build ungenutzten)
  SD-Karten-Pins, Bestätigung über Serienwiderstände in der Netzliste
- Vollständige HID-Consumer-Code-Tabelle aus der Referenz-Firmware
  (`CHROMECAST_COMMANDS`) als Kommentar in der Beispiel-YAML übernommen

**Automation-API:** `ble_hid_atv.send_key`, `ble_hid_atv.send_consumer`,
`ble_hid_atv.begin_voice_search` / `end_voice_search`, plus `on_connect`/
`on_disconnect`-Trigger.

**Validierung:** `esphome config` → "Configuration is valid!". `esphome
compile` → C++-Codegenerierung läuft fehlerfrei durch, generiertes `main.cpp`
zeigt korrekt aufgelöste Template-Instanzen. Echter Cross-Compile wie bei den
anderen Components durch den blockierten Espressif-Server-Download in dieser
Sandbox nicht möglich — API-Signaturen wurden deshalb zusätzlich manuell
gegen den echten ESPHome-Quellcode verifiziert (nicht nur auf Codegen-Erfolg
verlassen).

**Bekannte Einschränkungen / vor dem ersten Test zu prüfen:**
- Kein physischer "Voice"-Taster in unserer 23-Tasten-Belegung vorgesehen
  (Referenz-Firmware erlaubt beliebige Tastenbindung über die UI) — Beispiel-
  YAML bindet es probeweise an einen bestehenden Slot, muss final zugeordnet
  werden
- `esphome compile` mit echtem Internetzugriff einmal vor dem Flash laufen
  lassen (wie bei den anderen Components)
- ATVV-Protokolldetails (Capability-Version, Frame-Timing) sind aus einer
  funktionierenden Referenzimplementierung übernommen, aber nicht öffentlich
  von Google spezifiziert — falls sich Android-TV-Hosts anders verhalten als
  erwartet, ist Nachjustieren anhand von BLE-Sniffing-Logs nötig

## Display: kritischer Fund + eigene Component gebaut

**Wichtiger Fund:** ESPHome hat **keinen nativen Treiber für den 8-Bit-
Parallel-Bus (Intel-8080/"i80")**, den dieses Display nutzt (WR/RD/DC/CS +
8 Datenleitungen, kein SPI). Der eingebaute `ili9xxx`-Treiber erbt direkt von
`spi::SPIDevice` — SPI-only, nicht austauschbar. Ohne diesen Fund wäre die
komplette LVGL-UI-Planung hinfällig gewesen.

**Lösung:** Eigene Component `ili9341_i80` gebaut, die ESP-IDFs
`esp_lcd_panel_io_i80`-API direkt nutzt (bestätigt vorhanden in ESP-IDF
5.5.5, inkl. eigenem LVGL-Beispiel dafür) und dabei **ESPHomes eigene,
bereits geprüfte ILI9341-Init-Kommandotabelle wiederverwendet**
(`esphome/components/ili9xxx/ili9xxx_init.h`) — nur die Transportschicht
(SPI → Parallel-Bus) wurde ersetzt, nicht die Init-Sequenz neu erfunden.
Implementiert als reguläre `esphome::display::DisplayBuffer`-Subklasse, an
die sich `lvgl:` automatisch bindet wie an jedes andere Display.

**Alle Display/Touch/Hintergrundbeleuchtungs-Pins gegen die Referenz-
Firmware verifiziert:**

| Signal | Pin |
|---|---|
| LCD CS / DC / WR / RD | GPIO39 / 40 / 41 / 42 |
| LCD D0–D7 | GPIO48, 47, 21, 14, 13, 12, 11, 10 |
| LCD Enable (active-low) | GPIO38 |
| LCD Backlight (active-low, PWM) | GPIO9 |
| I2C SDA / SCL (TCA8418, MAX17048, LIS3DHTR, FT6206) | GPIO20 / GPIO19 |
| IR-Sender (**active-low**, Transistorstufe) | GPIO5 |
| IR-Empfänger | GPIO4 |

**Wichtige Korrektur gegenüber früheren Beispiel-Configs in diesem Projekt:**
Die I2C-Pins waren vorher mit Platzhaltern (GPIO21/22) befüllt — echte Pins
sind GPIO20 (SDA) / GPIO19 (SCL). ESPHome warnt hier, dass diese Pins auch
für USB-Serial-JTAG genutzt werden — unkritisch, da die Referenz-Firmware
exakt diese Pins produktiv nutzt und JTAG-Debugging im Betrieb nicht
gebraucht wird. Außerdem wurde bislang übersehen, dass der IR-Sender-Pin
**active-low** ist (Transistor-Treiberstufe) — muss beim `remote_transmitter`
berücksichtigt werden (in der Basis-Config als Kommentar vermerkt, `inverted`-
Handling noch in die IR-Beispiel-YAML nachzutragen).

**Validierung:** `esphome config` → "Configuration is valid!" (keine Warnungen
mehr nach Fixes). `esphome compile` → C++-Codegenerierung läuft fehlerfrei
durch, inkl. korrekt aufgelöster Klassenverweise im generierten Code. Echter
Cross-Compile weiterhin durch den blockierten Espressif-Server-Download
verhindert; API-Signaturen (`esp_lcd_i80_bus_config_t`,
`esp_lcd_panel_io_i80_config_t`, `esp_lcd_panel_io_tx_param/tx_color`,
`display::DisplayBuffer`-Interface) wurden gegen die echten ESP-IDF- und
ESPHome-Header geprüft, nicht geraten.

**Einschränkung:** Ein Display-Treiber lässt sich anders als die bisherigen
Components nicht allein durch Config-/Codegen-Validierung auf pixelgenaue
Korrektheit prüfen (Timing, exakte Command-Reihenfolge, Farbformat) — das
geht erst mit echter Hardware. Sollte beim ersten Test schwarzes/verzerrtes
Bild o. Ä. auftreten, ist das der wahrscheinlichste Debugging-Ansatzpunkt,
nicht die LVGL-Konfiguration selbst.

## LVGL-UI: Media-Player-, Licht- und Schalter-Karte gebaut

`esphome_components/openremote-base-display-lvgl.yaml` enthält das komplette
Grundgerüst: Display + Touch (`ft63x6`, nativ, kein Interrupt-Pin verdrahtet)
+ Hintergrundbeleuchtung + drei LVGL-Karten auf einer Seite:

- **Media-Player-Karte**: Cover-Art (`image:`-Widget + `image: platform:
  online_image`, per `homeassistant.text_sensor` auf `entity_picture`-
  Attribut nachgeladen), Titel/Interpret (`media_title`/`media_artist`-
  Attribute), Play/Pause/Skip-Buttons und Lautstärke-Slider (alle über
  `homeassistant.service`-Aufrufe an `media_player.*`)
- **Licht-Karte**: Toggle + Helligkeits-Slider, Zustand kommt über
  `homeassistant.binary_sensor` zurück und aktualisiert den Toggle
  (`lvgl.switch.update` mit `state: {checked: ...}`)
- **Schalter-Karte**: einfacher Toggle nach demselben Muster

**Validiert:** Schema und C++-Codegenerierung laufen komplett fehlerfrei und
warnungsfrei durch (inkl. Korrektur eines Deprecation-Warnings:
`online_image:` ist als Top-Level-Key veraltet, jetzt als
`image: - platform: online_image` geschrieben).

**Noch anzupassen vor dem echten Einsatz:** Entity-IDs (`media_player.
wohnzimmer`, `light.wohnzimmer`, `switch.ventilator`) sind Platzhalter,
HA-URL (`homeassistant.local:8123`) ggf. anpassen, Karten-Layout/-Größen sind
ein erster Entwurf für 240×320px und nicht final designt.

## Offene Punkte / noch zu klären (nur per Discord/Community lösbar)

- Rev6-spezifische Gehäuse-STLs (BuyDisplay-Variante) weiterhin nicht veröffentlicht
- Schrauben-Kopftyp weiterhin nicht offiziell dokumentiert
- LVGL-UI-Karten (Media-Player, Licht, Schalter): **fertig**, siehe Abschnitt
  oben — Entity-IDs vor dem Einsatz noch anzupassen

---

## UPDATE 2026-09-02 — Hardware da, BLE/Voice auf atv_voice umgestellt

Die Rev6-Hardware ist eingetroffen. Zwei Änderungen:

### 1. BLE-HID + ATVV: `ble_hid_atv` → `espidf_ble_keyboard` + `atv_voice`

`ble_hid_atv` war ein Port der Firmware **1.0** (8 kHz ADPCM, `esp32_ble_server`,
nie auf Hardware getestet). Die OpenRemote-Firmware ist inzwischen bei **3.56**
und macht **16 kHz** — der ganze Weg dahin (MTU 247, DLE, Verbindungsintervall,
`codec=0x03 frame=160`, Selbst-Fallback) steht im Changelog von
`OpenRemote_1.0.ino` (Versionen 3.51–3.56).

Entscheidung (mit dem Nutzer): `open-remote` auf dieselbe Kombi wie `wz-ir`
umstellen — `espidf_ble_keyboard` (Bluedroid-GATTS-Fork) + `atv_voice`. Die ist
auf echtem klassischem ESP32 auf 16 kHz erprobt (`wz-ir/HANDOVER.md`) und hat
Client-Rollen-MTU-Aushandlung, DLE und 15-ms-Intervall bereits drin (~Stand
Firmware 3.52). `ble_hid_atv` und die `esp32_ble`/`esp32_ble_server`-Blöcke sind
aus `esphomeremote.yaml` raus (Bluedroid hat nur einen globalen GATTS-Callback —
die beiden Stacks können nicht parallel laufen).

`ble_hid_atv/` bleibt als Ordner liegen, wird aber nicht mehr eingebunden.

Restliche Angleichung an Firmware 3.56 (160-B/20-ms-Framing, CAPS_RESP-Layout
mit Model-Byte, Nibble-Reihenfolge, Format-Gating gegen echte Link-Parameter,
GET_CAPS-Verzögerung, Fallback-Persistenz): **noch offen**, priorisierte Liste
mit Firmware-Zeilenverweisen in `esphomeremote-3.56-delta.md`. Erst nach einem
ersten erfolgreichen 16-kHz-Test auf der Rev6 angehen.

`wifi: power_save_mode` von `HIGH` auf `none` — für die BLE/WLAN-Koexistenz
während des Audio-Streamings Pflicht (wz-ir VERSUCH 29).

### 2. Gestufte Inbetriebnahme-Configs

Statt die komplette `esphomeremote.yaml` auf einmal zu flashen: vier isolierte
Test-Configs, jede mit `esphome compile` **echt gebaut** (nicht nur Codegen):

| Datei | Inhalt | Compile |
|---|---|---|
| `esphomeremote-test-1-display.yaml` | ili9341_i80 + ft63x6 + Backlight + Mini-LVGL | ✅ |
| `esphomeremote-test-2-keypad.yaml` | tca8418 (23 Tasten) + max17048 + lis3dh | ✅ |
| `esphomeremote-test-3-ir.yaml` | remote_transmitter/receiver + ir_learn | (läuft) |
| `esphomeremote-test-4-ble-voice.yaml` | espidf_ble_keyboard + atv_voice + I2S-Mic | (läuft) |

Ablauf und Prüfpunkte: `esphomeremote-flash-reihenfolge.md`.

**Board:** `esp32-s3-devkitc1-n16r8` (N16R8 = 16 MB Flash / 8 MB OPI-PSRAM,
Bestückung der Rev5/Rev6). `esp32:` ist bei der S3 die richtige Plattform.

**Toolchain:** `esphome` 2026.8.2 lokal auf der HA (Python 3.14). ESP-IDF 5.5.5
Toolchain im Cache. Der frühere Sandbox-Blocker (`dl.espressif.com`) besteht auf
dieser Maschine nicht — echte Cross-Compiles laufen durch.

### 3. Erste Hardware-Tests — zwei Component-Bugs gefunden

**Display (`ili9341_i80`):** Backlight ging an, Panel blieb schwarz. Zwei
Ursachen, beide durch Vergleich mit der Firmware gefunden:
- `PIN_LCD_EN` (GPIO38) ist active-low (LOW = Panel-Rail an). YAML hatte
  `inverted: true` + Component schrieb `digital_write(false)` → GPIO38 HIGH →
  Rail AUS. Fix: `inverted:` raus, Component treibt jetzt eine saubere
  HIGH→LOW-Flanke + 150 ms settle (Firmware: `lcdPowerOn()` macht
  HIGH→40ms→LOW→300ms).
- Kein Reset-Pin auf der Rev6 (`PIN_LCD_RST = -1`) → Component schickt jetzt
  einen Software-Reset (0x01) vor der Init-Sequenz.
- `invert_colors: true` gesetzt — BuyDisplay-Panel der Rev6 (Firmware 3.09/3.11).

**Keypad (`tca8418`):** Akku/Spannung ok, aber KEINE Taste kam durch. Ursache
(Firmware-Changelog 3.35: *"the INT line stayed asserted low"*): die INT-Leitung
des TCA8418 wird NICHT allein durch Schreiben von `INT_STAT` (0x02) freigegeben —
die GPIO-Interrupt-Status-Register `0x11/0x12/0x13` müssen vorher **gelesen**
werden. Sonst bleibt INT nach dem ersten (evtl. Spurious-)Event dauerhaft LOW →
keine weitere fallende Flanke → keine Taste. Fixes in `tca8418.cpp`:
`clear_interrupts_()` liest 0x11-0x13; `loop()` prüft jetzt den INT-**Pegel**
statt nur die Flanke (selbstheilend, wie die Firmware); Firmware-Init-Register
0x23/0x26/0x2C ergänzt.

**LIS3DH (`lis3dh`):** Bewegung wurde dauerhaft gemeldet, auch im Stillstand.
Ursache: ohne Hochpassfilter sieht der INT1-Generator permanent den 1g-
Schwerkraftvektor auf der vertikalen Achse. Fix (aus `configureLis3dhMotionWake`
der Firmware, ~Zeile 6708): CTRL_REG2 = 0x09 (HPIS1 + FDS → gefilterte Daten in
den INT1-Generator), nach ~480 ms Einschwingen ein Dummy-Read von REFERENCE
(0x26) für die Nulllage, INT1_SRC (0x31) lesen, dann erst INT1_CFG/CTRL_REG3
scharf schalten. 25 Hz Low-Power-Modus, nicht gelatcht (INT1 folgt dem
Bewegungszustand in Echtzeit).

Alle drei Fixes betreffen die geteilten Components → gelten auch für
`esphomeremote.yaml`.

**Ergebnis Test 1+2 auf echter Rev6-Hardware (2026-09-02):**
- ✅ Display + Touch + Display-/Tastenbeleuchtung
- ✅ Alle Matrix-Tasten (Keycodes stimmen mit der Zuordnungstabelle), Power-
  Taster (keycode 102)
- ✅ Akku/Spannung (MAX17048)
- ✅ Bewegungserkennung (LIS3DH)
- I2C läuft mit 50 kHz; `bus successfully recovered` beim Start ist normal.

Flash geht per OTA (`esphome run ... --device <ip>`), Gerät hängt im WLAN.

### 4. Weitere Hardware-Funde + Menü-UI (2026-09-02/03)

**IR-Empfänger braucht Strom über GPIO6** (`PIN_IR_VCC`, active-high — die
Firmware treibt das in `lcdPowerOn()`). Ohne das ist der IRM-V838M3 stromlos,
`remote_receiver` empfängt nichts. In `esphomeremote-test-3-ir.yaml` und
`esphomeremote.yaml` als `switch: gpio` GPIO6 ALWAYS_ON ergänzt. Beim ersten Test
ohne Fix kamen nur kurze Pronto-Fragmente (Umgebungsrauschen). IR/Mic-Hardware
war beim Testen noch nicht angeschlossen — Test 3/4 stehen aus.

**`esphomeremote-test-5-ui.yaml`** — das Bildschirm-Menü, wie vom Nutzer
gewünscht:
- Zwei Modi über einen `menu_active`-Global. GERÄTEMODUS: Tasten → HA-Services
  an `media_player.tv_wohnzimmer` (Platzhalter für IR/BLE). MENÜMODUS
  (MENU-Taste S23 schaltet um): D-Pad + OK navigieren das LVGL-Menü.
  ZURÜCK (S18) blättert zurück bzw. verlässt den Menümodus.
- LVGL `keypads:` liest die D-Pad-`binary_sensor`s (keycodes 14/35/15/32/34)
  als Keypad-Indev → up→prev, down→next, left/right an Slider, OK→enter.
  Fokus-Navigation bleibt automatisch auf der sichtbaren Seite (LVGL
  überspringt versteckte = nicht angezeigte Pages).
- 3 Pages: `idle_page` (Uhr + "jetzt läuft"), `menu_page` (3 Kacheln),
  `media_page` (Cover via online_image, Titel, ⏮⏯⏭, Lautstärke-Slider).
- Dunkles Design über `style_definitions`, Fokusrahmen als `focused:`-State je
  Widget. LV_SYMBOL-Glyphen (`""` etc.) rendern aus den montserrat-Fonts.
- Kompiliert (RAM 34 %, Flash 16.5 %), fehlerfrei geflasht.
- Muss noch in `esphomeremote.yaml` integriert werden (dort ist noch das alte
  LVGL mit den Klingel-/Aktivität-Pages).

### 5. Menü-UI Neubau (2026-09-03) — `esphomeremote-test-5-ui.yaml`

Nach mehreren Hardware-Runden komplett überarbeitet. Gerät `esphomeremote-t5`
(192.168.1.69). RAM 35 %, Flash 14.5 %, sauber geflasht + gebootet.

**Eine Schriftart, echte Umlaute:** nur noch Audiowide
(`fonts/Audiowide-Regular.ttf`, IDs `a13`/`a15`/`a18`, `bpp: 4`,
`default_font: a15`). Montserrat/gfonts raus. Audiowide hat vollständige
Umlaut-Abdeckung (per fontTools geprüft) → keine Transliteration mehr, keine
gemischten Fonts. Icons ersetzt durch ASCII (`>` Chevron, `<<` `> ||` `>>` im
Media-Player). Glyphen-String explizit gesetzt.

**Manuelle Fokus-Navigation statt LVGL-`keypads:`.** Grund: in der gebündelten
LVGL 9.5 hängt `lv_group_set_default` ALLE fokussierbaren Widgets aller Pages in
EINE Gruppe; `focus_next_core` (lv_group.c:467) überspringt nur Widgets mit
`LV_OBJ_FLAG_HIDDEN` im Parent-Pfad — nicht-aktive Screens sind aber nicht
"hidden" → Fokus leakt über Seiten, man kann "unter" den letzten Button
rutschen. Lösung:
- `keypads:`-Block entfernt, D-Pad-`binary_sensor`s entfernt.
- Globals `fidx` (Index), `cur_focus` (`void*` auf das fokussierte Widget).
- Script `focus_apply`: `switch(cur_page)` baut die Liste sichtbarer Buttons der
  Seite, klemmt `fidx` in `[0, n-1]`, setzt `LV_STATE_FOCUSED` per
  `lv_obj_add_state`/`lv_obj_remove_state`, `lv_obj_scroll_to_view` scrollt den
  Button in den sichtbaren Bereich.
- `tca8418.on_key` (ein Lambda): keycode 31 = Menü auf/zu (go_home/open_menu),
  13 = nav_back, 14/15 = focus_prev, 35/32 = focus_next, 34 = activate. Alle
  vier D-Pad-Richtungen bewegen also ±1 in der flachen Seitenliste (löst
  "muss nach unten drücken statt links/rechts").
- Script `activate`: `lv_obj_send_event(cur_focus, LV_EVENT_CLICKED, nullptr)`
  → löst denselben `on_click`-Handler aus wie ein Fingertipp. Touch und D-Pad
  teilen sich also die Logik.
- Jede Page `on_load`: `cur_page` setzen, `fidx = 0` (Media: `fidx = 1` →
  Play/Pause), `focus_apply`.

**Seiten (9):** page_home(0), page_menu(1), page_rooms(2), page_room_wz(3),
page_devices(4), page_smarthome(5), page_media(6), page_doorbell(7),
page_settings(8). Menü-Seiten sind Flex-Spalten mit Scroll; max. eine
Verschachtelungsebene (die 3er-Button-Reihen) — tiefere Flex-Verschachtelung
zerschoss vorher Touch-Hit-Testing.

**Echte HA-Entities** (aus `/config/.storage/core.entity_registry` verifiziert):
`cover.wohnzimmer_rollo` (nicht `cover.rollo_wohnzimmer`), `light.wohnzimmer`,
`media_player.tv_wohnzimmer`, `lock.haustur`,
`input_boolean.dashboard_wz_kamera`.

**Wohnzimmer-Seite:** Licht-Button (`light.toggle`, Label zeigt "Licht: an/aus"
via text_sensor), Dunkler/Heller (`light.turn_on` mit `brightness_step_pct`
∓15), Rollo-Status-Label + Auf/Stop/Zu (`cover.open/stop/close_cover` auf
`cover.wohnzimmer_rollo`), Media-Kachel → page_media.

**SmartHome-Seite:** WZ-Kamera-Status + "WZ Kamera umschalten"
(`input_boolean.toggle`), neuer Punkt "Geräte-Einstellungen" → page_settings.

**page_settings (neu):** "Tastenbeleuchtung umschalten". Global `kbd_light_on`
(`restore_value: true`, `initial_value: false`), `button_backlight` jetzt
`restore_mode: ALWAYS_OFF`. `wake_backlight` schaltet die Tastenbeleuchtung nur
ein, wenn `kbd_light_on`. "Weitere Einstellungen folgen."

**Menü-Positionsgedächtnis:** unverändert — `last_menu_page`/`last_menu_ms`,
10-Sekunden-Fenster in `open_menu`.

**Offen:** Touch-Kalibrierung (mirror_x+mirror_y unbestätigt, 4-Ecken-Test
steht aus). Danach Integration in `esphomeremote.yaml`.

### 6. Display-Treiber-Neubau + Font-Bugfix (2026-09-03, nachts)

Nutzer meldete: „das display zuckt bei mir und macht fehler, aber nicht bei der
Open Remote original software" und „ganz viele leere Kästen habe ich in meiner
Schrift".

**Font: das Leerzeichen fehlte im Glyphensatz.** Beim Umstellen auf Audiowide
wurde der `glyphs:`-String ohne `U+0020` geschrieben, also rendert jeder
Wortzwischenraum als .notdef-Kasten — genau das, was der Nutzer als
„Info tast**X**oder**X**tippen" beschrieben hat. Prüfskript über alle `text:`-
Werte und `std::string("…")`-Literale bestätigte: **nur** das Leerzeichen fehlte.
Jetzt vollständiger Satz aus 168 Zeichen (ASCII 0x20-0x7E + Latin-1-Akzente +
typografische Anführungszeichen), per fontTools gegen die Audiowide-cmap
geprüft — die Schrift enthält alle 168.

**Display: `ili9341_i80` hat den kompletten Framebuffer alle 16 ms rausgeschoben.**
Drei zusammenwirkende Fehler im eigenen Component:
1. `draw_pixels_at()` war nicht überschrieben → ESPHomes Basisklasse lief
   `draw_absolute_pixel_internal()` **pro Pixel** (virtueller Call + Spiegel-
   rechnung + Farbkonvertierung). Ursache der `lvgl took a long time
   for an operation (159 ms)`-Warnungen.
2. `update()` schickte per `flush_buffer_()` **immer das ganze Bild**
   (153.600 B), 60×/s, unabhängig davon ob sich etwas geändert hatte.
3. `esp_lcd_panel_io_tx_color()` ist **asynchron** (GDMA), es wurde aber nie auf
   den Abschluss gewartet und `trans_queue_depth` stand auf 10. LVGL hat also in
   denselben Puffer gerendert, den die DMA noch auslas → Tearing/Artefakte.

Fix: `draw_pixels_at()` überschrieben — schickt das von LVGL gelieferte
Rechteck (gepacktes RGB565 big-endian, exakt das Busformat) **ohne Kopie und
ohne Konvertierung** direkt ans Panel. `on_color_trans_done`-Callback registriert
und nach jedem Transfer darauf gewartet, bevor der Quellpuffer freigegeben wird;
`trans_queue_depth` auf 2. `update()` schiebt nichts mehr, `update_interval` auf
1 s. Neue YAML-Option `bus_frequency` (Default 20 MHz). LVGL `buffer_size: 25%`
(38 KB, passt in den internen RAM statt ins langsamere PSRAM).
Ergebnis: sauberer Boot, **keine einzige Warnung in 70 s Log**.

Das entspricht bewusst dem synchronen Pfad der Referenz-Firmware: die hat zwei
wählbare Treiber (`displayDriverChoice`: 0 = LovyanGFX/DMA, 1 = Arduino_GFX
synchron) und lädt per `preferences.getUChar("dispDrv", 1)` **den synchronen als
Default**.

**Touch-Kalibrierung bestätigt.** Firmware `readTouchSample()` (Zeile 6568):
`x = (LCD_W-1) - rawX; y = (LCD_H-1) - rawY` — identisch zu unserem
`transform: {mirror_x: true, mirror_y: true}`. Kein Handlungsbedarf.

**Touch-Treiber-Auswahl gibt es nicht mehr zu portieren.** Firmware 3.20 hat den
„Touch Driver"-Dropdown entfernt: Adafruit und BuyDisplay waren byte-gleich, der
FT5x06-Pfad tot. Die ganze Ghost-Touch-Saga (2.38-2.46) betraf ausschließlich das
Adafruit-Panel; das hier verbaute BuyDisplay zeigt sie laut 3.20 nicht.

**Root Cause „kein Button tut etwas":** Der ESPHome-Config-Entry des Geräts hat
`options: {'allow_service_calls': False}` (nachgesehen in
`/config/.storage/core.config_entries`). Damit blockiert HA **jeden**
`homeassistant.service`-Aufruf des Geräts — Media, Rollo, Licht, SmartHome, Tür.
Muss der Nutzer selbst umlegen (Einstellungen → Geräte & Dienste → ESPHome →
Gerät → Zahnrad → „Diesem Gerät erlauben, Home Assistant Aktionen auszuführen").

**Menü:** Wrap-Around-Navigation (unter dem letzten Button geht es oben weiter),
Schlafzimmer ist ein echter Raum geworden (`light.schlafzimmer`,
`cover.schlafzimmer_rollo`), Menü-Positionsgedächtnis deckt jetzt auch
Geräte-Einstellungen und Schlafzimmer ab.

**Voice:** in `esphomeremote-test-5-ui.yaml` bewusst nicht enthalten (reine UI-
Config). ATVV lebt in `esphomeremote.yaml` über `atv_voice` und steht auf
Firmware-Stand ~3.52; die offenen Punkte zu 3.56 stehen in
`esphomeremote-3.56-delta.md`. Bewusst NICHT blind umgesetzt: Mikrofon ist
hardwareseitig noch nicht angeschlossen, die Änderungen (Framing 160 B/20 ms,
Nibble-Reihenfolge, CAPS_RESP-Layout) sind nur am Serial-Log gegen echte
Google-TV-Hardware verifizierbar, und die Nibble-Reihenfolge widerspricht einer
früheren Verifikation gegen Sony/Google-Sniffs.

### 7. Navigation: flache Liste → Zeilen/Spalten-Raster (2026-09-03)

`allow_service_calls` war tatsächlich die einzige Ursache dafür, dass Rollo,
Licht und Media nichts taten — Beleg im HA-Core-Log: jeder Tastendruck kam mit
korrektem Dienst und korrekter Entity an und wurde mit „Service call … rejected;
… you can enable this functionality in the options flow" abgewiesen. Nutzer hat
den Haken gesetzt, seitdem funktioniert alles.

Danach die Rückmeldung: „wenn ich runter drücke geht er auch die button von
links nach rechts weiter er soll aber nur runter gehen." Die manuelle Navigation
war eine **flache Liste** (`fidx`), alle vier Richtungen ±1 — nebeneinander-
liegende Buttons wurden also beim Runterdrücken einzeln durchlaufen.

Umgebaut auf ein **Raster**: `focus_apply` baut pro Seite `g[6][3]` + `rl[]`
(Buttons je Zeile) über eine lokale `row(a,b,c)`-Lambda. Globals jetzt
`frow`/`fcol` (Position) und `fdrow`/`fdcol` (Bewegung) statt `fidx`/`fdelta`.
Scripts `focus_up`/`focus_down`/`focus_left`/`focus_right`, in `on_key` einzeln
auf die Keycodes 14/35/15/32 gelegt.

Semantik:
- **Hoch/Runter** wechselt die Zeile, oben/unten umlaufend. Die Spalte wird in
  die neue Zeile **eingepasst** (`if (c >= rl[r]) c = rl[r]-1`), nicht
  zurückgesetzt — aus der rechten Spalte kommt man rechts wieder heraus.
- **Links/Rechts** bewegt sich **nur innerhalb** der Zeile, dort umlaufend.
- Seitenwechsel (`fdrow == fdcol == 0`) validiert die Position nur.

Zeilen: WZ/SZ = Licht / Dimmen(2) / Rollo(3) / [Media] / Zurück; Media =
Transport(3) / Lautstärke(2) / Zurück; Menü/Räume/SmartHome/Settings je ein
Eintrag pro Zeile.

### 8. Layout-Fixes Wohnzimmer/Schlafzimmer (2026-09-03)

Foto vom Nutzer: Buttons „nicht ganz drauf", Seite startet halb gescrollt.
Drei Ursachen:

1. **Seite zu hoch.** Nachgerechnet 338 px Inhalt auf 320 px Display
   (pad_top 10 + pad_bottom 12 + Titel 26 + Licht 44 + Dimm-Zeile 40 +
   Rollo-Label 22 + Rollo-Zeile 42 + Media 54 + Zurück 40 + 6 Luecken à 8).
   Gekürzt auf `pad_row: 5`, `pad_top/pad_bottom: 6`, Media-Kachel 54 → 46
   (`pad_all: 12`) → **300 px**. Seite scrollt nicht mehr.
2. **`long_mode: DOT` kürzt in LVGL nur bei fester Höhe.** Ohne `height:`
   darf das Label beliebig hoch werden und bricht um — der Media-Titel
   („Bambu Lab A2L: Großer 3D-…") stand auf zwei Zeilen und sprengte die
   Kachel. Feste Höhe gesetzt bei `tile_wz_media_sub` (20), `idle_title` (20),
   `idle_artist` (18), `media_title` (20), `media_artist` (18).
3. **`lv_obj_scroll_to_view()` lief im `on_load`**, also bevor LVGL die Seite
   ausgemessen hatte → Sprung an eine falsche Position. Jetzt: `focus_apply`
   unterscheidet über `moved = (fdrow||fdcol)` — beim Seitenwechsel
   `lv_obj_scroll_to_y(scr, 0, LV_ANIM_OFF)` (immer oben anfangen), beim
   Navigieren `lv_obj_update_layout(scr)` vor `lv_obj_scroll_to_view()`.

Offen/zu beobachten: auf dem Foto hatten **zwei** Buttons gleichzeitig den
Fokusrahmen. `focused:` mappt in ESPHome sauber auf `LV_STATE_FOCUSED`
(`lvgl/defines.py:508`), und `focus_apply` löscht den Zustand bei allen anderen
Rastereintraegen — die Logik stimmt also. Vermutlich ein Nachzieher des
Teil-Neuzeichners auf der gescrollten Seite; sollte mit Punkt 1 erledigt sein.
Wenn nicht, ist es ein echter Fehler und muss separat verfolgt werden.

### 9. Voice-Bring-up: Mic-Datenleitung unterbrochen (2026-09-03)

**BLE/ATVV funktioniert vollständig.** Pairing mit dem Google TV erfolgreich
(Slot 0, Identity AA:BB:CC:DD:EE:01), **MTU 517** (die Referenz-Firmware erreicht
247), Verbindungsintervall 15 ms, Capability-Handshake sauber (Host meldet
`codecs 0x0003`), 234 Frames pro Abfrage ohne gemeldete Drops. Der Transport ist
besser als in der Referenz-Firmware.

**Aber: `Mic level: peak 0 raw, avg 0` — das Mikrofon liefert exakt Nullen.**

Wichtige Korrektur an früheren Notizen: **Rev5 hat gar kein Mikrofon**
(`MIC1 im Board? False` im Rev5-PCB). Die Referenz-Firmware ist Rev5 und
enthält "Rev 5 pin map" im Header — jeder Abgleich der Mic-Pins gegen diese
Firmware war wertlos. Die Pins wurden jetzt direkt aus dem **Rev6-PCB**
verifiziert (`OpenRemote.kicad_pcb`, U7 = ESP32-S3):

| Netz | ESP32 | MIC1-Pin | dazwischen |
|---|---|---|---|
| SD_SCK  | IO15 | Pad 4 SCK | R45 4k7 |
| SD_MOSI | IO17 | Pad 5 WS  | R46 4k7 |
| SD_MISO | IO7  | Pad 3 SD  | R47 4k7 |
| MIC_VDD | IO45 | Pad 2 VDD | D3 (LED) + R5 2k2 als Anzeige |
| GND     | –    | Pad 1 + Pad 6 (L/R) | L/R→GND = **linker Kanal** |

Alle Pins in `esphomeremote-test-4-ble-voice.yaml` sind damit korrekt.

**Ausgeschlossen durch Messung:**
- SD-CS (GPIO18) auf HIGH ziehen: ändert nichts
- `channel: left` → peak 0; `channel: right` → **ebenfalls** peak 0
  (beide Frame-Hälften leer, also kein Slot-Problem)
- I2S selbst läuft: 60000 Samples in 4 s = exakt 16 kHz, pünktlich

**Diagnose per Pin-Test** (Template-Button "Pin-Test Mic-Leitungen" in der
Config: GPIO als Eingang mit internem Pull-up bzw. Pull-down lesen), einmal mit
`MIC_VDD` an und einmal aus:

```
MIC_VDD AN :  GPIO7 floatet   GPIO15 floatet     GPIO17 floatet
MIC_VDD AUS:  GPIO7 floatet   GPIO15 LOW  <---   GPIO17 LOW  <---
```

Bei abgeschalteter Versorgung klemmen die ESD-Schutzdioden des Mikrofons seine
Eingangspins auf VDD=0 und ziehen SCK/WS über die 4k7 nach unten. Das beweist:
1. GPIO45 steuert die Mic-Versorgung tatsächlich,
2. das Modul ist auf SCK und WS elektrisch verbunden,
3. **die Datenleitung MIC1 Pad 3 (SD) → R47 → IO7 ist unterbrochen** — GPIO7
   reagiert überhaupt nicht auf den Versorgungswechsel.

**Verdächtige, in dieser Reihenfolge:** MIC1 Pad 3 ist handgelötet (`MIC1` steht
NICHT in der CPL, Footprint `MS3625_Module_Back_Mount_SMD`), R45/R46/R47 dagegen
schon — und R45/R46 funktionieren nachweislich. Also zuerst die Lötstelle am
Modul nachlöten.

Physische Positionen (PCB-Koordinaten):
- MIC1 @(142.5, 102.0), Pads: GND(139.96,105.81) VDD(142.50,105.81)
  **SD(145.04,105.81)** / L/R(139.96,98.19) WS(142.50,98.19) SCK(145.04,98.19)
- R47 @(144.0, 112.0): Pad1 (Mic-Seite) @(144.00,111.17), Pad2 (SD_MISO) @(144.00,112.83)
- TP20 = Messpunkt auf SD_MISO, Unterseite (B.Cu) @(70.22,110.05)

Durchgangsprüfung: MIC1 Pad3 ↔ R47 Pad1 = 0 Ω · R47 = 4k7 · R47 Pad2 ↔ TP20 = 0 Ω.

### 10. Sprachsuche läuft — Ursache war dreifach (2026-09-03)

**Ergebnis:** `Mic level: peak 1570 raw, avg 95, clipped 0/99584 samples`,
389 Frames pro Abfrage, keine Drops. Mit Gain 6.5 entspricht das ~31 %
Vollaussteuerung — die Referenz-Firmware misst an Sonys Original-Fernbedienung
34 %.

Drei unabhängige Fehler, die sich gegenseitig maskiert haben:

1. **MIC1 Pad 3 (SD) nicht durchkontaktiert.** Nachgewiesen über ESD-Klemmtest:
   Pin als Eingang mit Pull-up/Pull-down lesen, einmal mit `MIC_VDD` an und
   einmal aus. Ist das Modul verbunden, klemmen seine Schutzdioden den Pin bei
   abgeschalteter Versorgung auf LOW. GPIO7 reagierte nicht → Leitung offen.
   Vom Nutzer nachgelötet.
2. **L/R (Pad 6) ohne definierten Pegel.** Danach klemmte GPIO7 zwar, das
   Mikrofon blieb aber stumm. Ohne festen L/R-Pegel sendet das MS3625 in
   **keinem** Slot — deshalb lieferten `channel: left` UND `channel: right`
   exakt Null, und jeder Kanaltest davor war bedeutungslos. Nutzer hat Pad 6
   auf Pad 1 gebrückt (beide GND laut Netzliste). Danach trieb GPIO7 die
   Leitung aktiv → Chip in Betrieb.
   Ob das PCB Pad 6 wirklich nicht erdet, blieb offen: Konstruktions- und
   Fertigungsdaten zeigen Leiterbahn + gebohrtes Via. Vermutlich schlechter
   Prüfspitzenkontakt beim Messen. Die Brücke ist trotzdem korrekt (entspricht
   dem Schaltplan) und bleibt drin.
3. **Falscher Slot.** Entscheidend war die neu eingebaute Rohbyte-Ausgabe in
   `atv_voice::on_mic_data_`: im rechten Slot kam `01 00 00 00` — nur das
   letzte Bit gesetzt, der Rand des benachbarten Slots. Also liegen die Daten
   links, wie L/R→GND es vorgibt. Zurück auf `channel: left`.

**Merksatz für die Zukunft:** Nicht auf den fertigen Pegel schauen, sondern
früh die Rohbytes loggen. `peak 0` sagt nur „nichts angekommen"; `01 00 00 00`
sagt „falscher Slot".

**Diagnosewerkzeuge, die in der Config bleiben sollten:**
- Template-Button „Pin-Test Mic-Leitungen" (ESD-Klemmtest auf GPIO7/15/17)
- `mic_power` / `sd_power_off` / `sd_cs_idle` als schaltbare Entities statt
  `internal: true` — sonst kostet jeder Versuch einen Flash-Zyklus
- Rohbyte-Ausgabe in `atv_voice.cpp` (einmal pro Abfrage)

**Wichtig für den Werkzeugkasten:** `esphome run` bleibt nach dem Upload im
Log-Stream hängen und belegt eine der 5 API-Verbindungen. Acht Stück davon
hatten das Gerät blockiert („Max connections (5), rejecting"). Ab jetzt
`esphome run --no-logs` bzw. `esphome upload`.
Fernsteuerung der Buttons/Switches über `aioesphomeapi` direkt (Skripte in
scratchpad: `orcmd.py`, `mictest.sh`) — HA muss das Gerät dafür nicht adoptiert
haben.

### 11. Menü, BLE und Sprachsuche vereint (2026-09-03, nachmittags)

`esphomeremote-test-5-ui.yaml` ist jetzt die Gesamtfirmware: LVGL-Menü, Tastatur,
BLE-HID, Mikrofon und ATVV zusammen. RAM 46,5 %, Flash 20,7 %, sauberer Boot.

**Zusammenführung.** Gerätename bleibt `esphomeremote-t5` — bewusst, sonst legt HA
einen neuen Config-Entry an und `allow_service_calls` steht wieder auf False.
Nötig waren: Bluedroid-`sdkconfig_options`, `power_save_mode: none`,
`espidf_ble_keyboard` + `atv_voice` in `external_components`, Mic-/SD-Schalter
und der I2S-Block. **Stolperstein:** `espidf_ble_keyboard` ruft `App.get_buttons()`
auf, und die Methode existiert nur, wenn ESPHome `USE_BUTTON` definiert — also
nur mit mindestens einer Button-Entity in der Config. Ohne die bricht der Build
mit „class esphome::Application has no member named get_buttons" ab. Zwei
Template-Buttons (Pairing, Sprachsuche) lösen das.

**Activities.** Global `activity` (restore_value, 0 = TV). Die Tastenzuordnung
auf dem Homescreen hängt in einem `switch (id(activity))` — weitere Aktivitäten
sind ein zusätzlicher `case`. Aktivität 0 schickt D-Pad/OK/Zurück/Home/Lautstärke/
Mute/Transport/Power als Consumer-Codes per BLE an den Streamer (Codes aus
`CHROMECAST_COMMANDS` der Referenz-Firmware).

**Record-Taste = Keycode 45** (row 4, col 4, am Gerät ausgelesen) ist die
Sprachtaste: gedrückt halten startet den Stream, Loslassen beendet ihn. Muss im
`on_key`-Lambda VOR dem `if (!pressed) return;` stehen, sonst fehlt das
Loslassen.

**Durchgängiges UI.** Alle Listen-, Aktions- und Zurück-Buttons und alle Kacheln
einheitlich **44 px** (vorher 34/38/40/42/46/54 gemischt). Schalter zeigen den
Zustand nach demselben Muster: Name links, rechts **AN** grün / **AUS** grau.
Bewusst kein LVGL-Switch-Widget — das bräuchte eine eigene Fokus- und
Auslöselogik neben der manuellen Tastensteuerung.

**Scroll-Fix.** In `focus_apply`: steht der Fokus in Zeile 0, wird
`lv_obj_scroll_to_y(scr, 0)` benutzt statt `lv_obj_scroll_to_view`. Letzteres
scrollt nur so weit, dass der Button gerade sichtbar ist — die Überschrift
darüber blieb abgeschnitten.

**Menüstruktur:** Menü → Räume / Geräte / SmartHome / **Einstellungen**.
Einstellungen enthält Tastenbeleuchtung und **Bluetooth** (vier Slots, Auswahl
markiert, „Kopplung löschen" wirkt auf den zuletzt gewählten Slot).
SmartHome enthält WZ-Kamera und **Sleeptimer**.

**Aufspringende Seiten** nach dem Klingel-Muster: Haustür, **Wohnungstür** (neu,
`lock.open` auf `lock.wohnungstur`) und **Sleeptimer**. Letzterer wird von
`input_boolean.dashboard_wz_sleeptimer` getriggert — das Flag wird nur gelesen,
nie geschaltet. Gesteuert wird der Timer: `timer.change` 00:30:00 für +30 und
`script.sleeptimer_abbrechen` fürs Abbrechen. **Nicht `timer.finish`** — das
würde die Auto-Aus-Automation auslösen.

Auf beiden Klingelseiten ein Schalter gegen automatisches Öffnen, jeweils mit
der zur Tür passenden Entity (`haustur_offnen_erlauben` /
`wohnungstur_offnen_erlauben`).

**Sprachsuche am TV bestätigt:** Erkennung kommt durch.

**Offen:** Framing auf 160 B / 20 ms (50 statt 62,5 Frames/s) wie Firmware 3.56,
CAPS_RESP meldet weiterhin codec 0x02 / frame 20 statt 0x03 / 160. Slot-Anzeige
„belegt/frei" im Bluetooth-Menü. IR weiterhin nicht angeschlossen.

### 12. Apps, Szenen, Küche, Tastenwiederholung (2026-09-03, spät)

**App-Paketnamen NICHT raten — am Gerät auslesen.** `media_player.tv_wohnzimmer_android`
führt in `app_id` die gerade laufende App. Ein Skript, das das Attribut alle 2 s
pollt und Wechsel protokolliert (`scratchpad/appwatch.sh`), liefert die exakten
Namen, während der Nutzer die Apps einmal öffnet. Meine Vermutung für SmartTube
(`com.teamsmart.videomanager.tv`) war falsch — richtig ist `org.smarttube.stable`.
Gemessen:

| App | Paket |
|---|---|
| SmartTube | `org.smarttube.stable` |
| Netflix | `com.netflix.ninja` |
| Disney+ | `com.disney.disneyplus` |
| Kodi | `org.xbmc.kodi` |
| RTL+ | `de.rtli.tvnow` |
| Amazon Prime | `com.amazon.amazonvideo.livingroom` |

Gestartet mit `remote.turn_on` auf `remote.tv_wohnzimmer_streamer`,
`activity: "market://launch?id=<paket>"` — funktioniert auch ohne Web-Deeplink,
den SmartTube und Kodi nicht haben.

**Tastenwiederholung.** Der TCA8418 liefert nur Druck und Loslassen, keine
Wiederholung. Umgesetzt über ein ausgelagertes Skript `dispatch_key` (führt die
Aktion für `id(cur_key)` aus) plus `key_repeat` (450 ms Vorlauf, dann alle
130 ms, solange `held_key != 0`). Erstdruck und Wiederholung teilen sich damit
zwingend dieselbe Logik. Wiederholend nur D-Pad und Lautstärke; OK, Zurück,
Home, Power, Mute und Transport bewusst nicht.

**Hochheben statt Bewegung.** Der LIS3DH kann beides nicht gleichzeitig: mit
gesetztem FDS-Bit stehen HOCHPASSGEFILTERTE Werte in den Ausgangsregistern, die
Schwerkraft ist herausgerechnet — genau die braucht der Neigungswinkel. Deshalb
zwei Chip-Konfigurationen, wie in der Referenz-Firmware
(`configureLis3dhMotionWake` vs `configureLis3dhBleIdleOrientation`):

```
Bewegung:   CTRL_REG2=0x09 (HPIS1+FDS), INT1_CFG=0x2A, CTRL_REG3=0x40 -> GPIO2
Hochheben:  CTRL_REG2=0x00, INT aus, OUT_X/Y/Z (0x28|0x80) alle 250 ms pollen
```
Winkel gegen die Lage beim Ausgehen des Displays; Schwellen 25°/40°/60°.
**Der Winkel muss 750 ms halten** (3 Messungen) — Wackeln auf der Couch kippt
kurz und fällt zurück, echtes Hochnehmen hält die Lage. Register werden per
`write_byte`/`read_bytes` direkt aus Lambdas geschrieben, der Component brauchte
keine Änderung.

**Räume:** Wohnzimmer bekam Szenen (TV/Kino/Hell nebeneinander). **Küche** neu:
Kochen (`input_boolean.kochen`), Dunstabzug (`fan.dunstabzugshaube`, 4 Stufen à
25 %), Licht (`light.lampe_dunstabzug`), Backofen als reine Anzeige
(`sensor.backofen_status`), plus vier Szenen. Die Küchen-Szenen stehen je in
einer eigenen Zeile: bei drei Spalten bleiben ~50 px Textbreite, „Küchenzeile"
passt dort nicht — abgeschnittene Beschriftungen sind ausdrücklich unerwünscht.

**Stolperfallen, zweimal reingelaufen:**
1. Der `button:`-Block ist PFLICHT (siehe Abschnitt 11). Beim Ersetzen des
   Bereichs „on_key bis binary_sensor" wurde er mitgelöscht — Kommentar steht
   jetzt davor.
2. **Nie zwei `esphome run` gleichzeitig** im selben Build-Verzeichnis: sie
   überschreiben sich (`FAILED: .bin_timestamp`). Und einen laufenden Build
   nicht mitten im Linken abschießen, sonst bleibt eine halbe `.elf` liegen
   („Failed to read a valid ELF header"); dann `esphomeremote-t5.elf`, `.bin` und
   `.bin_timestamp` löschen und neu bauen. Vor jedem Build `ps | grep esphome run`.

**LVGL-Positionierung:** `align` bezieht sich auf die INHALTSFLÄCHE des Eltern-
objekts, also nach Abzug von `pad_top`/`pad_bottom`. Auf dem Homescreen
(pad_top 10, pad_bottom 12) rutschte dadurch alles von oben 10 px runter und der
von unten ausgerichtete Knopf 12 px hoch — 22 px, die sich zu einer Überlappung
addierten.

### 13. App-Start auf dem Streamer — was wirklich funktioniert (2026-09-03)

Am Gerät durchgetestet, nicht aus der Dokumentation übernommen. Ziel ist
`remote.tv_wohnzimmer_streamer` (androidtv_remote, **192.168.1.79**), Kontrolle
über `app_id` von `media_player.tv_wohnzimmer_android`.

**Funktioniert NICHT:**
- `market://launch?id=<paket>` — der Play Store ist vorhanden
  (`market://details?id=…` öffnet ihn), unterstützt diese Variante aber nicht mehr
- `intent:#Intent;package=…;end` — wird nicht angenommen
- reiner Paketname — wird nicht angenommen

**Funktioniert:** ausschließlich der Web-Deeplink der jeweiligen App.

| App | Deeplink |
|---|---|
| Netflix | `https://www.netflix.com/browse` (ohne `/browse` greift er nicht) |
| Disney+ | `https://www.disneyplus.com/` |
| Amazon Prime | `https://app.primevideo.com/` |
| RTL+ | `https://plus.rtl.de/` |

**Keine Möglichkeit:** SmartTube und Kodi haben keinen Deeplink. SmartTube
liesse sich über `https://www.youtube.com/` erreichen, wenn das offizielle
YouTube deaktiviert wird — sonst gewinnt das Original die Auflösung. Kodi
bräuchte ADB, und zwar auf dem **Streamer (192.168.1.79)**. Die vorhandene,
vom Nutzer deaktivierte `androidtv`-Integration zeigt auf **192.168.1.41**, also
den Sony-Fernseher — sie zu reaktivieren hilft für die Streamer-Apps NICHT.

**Wichtiger Nebenbefund:** Die App `com.esaba.downloader` hatte sich als
Standard-Handler für `http(s)` UND `market://` eingetragen und fing jeden
App-Link ab (jeder Startversuch landete bei ihr). Nach ihrer Deinstallation
funktionierten die Deeplinks sofort. Wenn App-Starts auf einem Android-TV
scheitern, zuerst prüfen, welche App die Link-Schemata beansprucht.

**ADB war nie Teil dieses Projekts** — die Sprachsuche läuft komplett über
BLE/ATVV. Nicht verwechseln.

### 14. Korrektur zu Abschnitt 13: ALLE Apps über ADB (2026-09-03)

Abschnitt 13 ist überholt. Auf Wunsch des Nutzers laufen alle sechs Apps
einheitlich über ADB — nicht vier per Deeplink und zwei Sonderfälle.

**ADB-Integration für den Streamer angelegt.** Port 5555 war auf 192.168.1.79
UND 192.168.1.41 bereits offen, der Schlüssel lag schon unter
`/config/.storage/androidtv_adbkey`. Anlegen per REST-Config-Flow (NICHT über
WebSocket — `config_entries/flow` gibt es dort nicht):

```bash
POST /api/config/config_entries/flow           {"handler":"androidtv"}
POST /api/config/config_entries/flow/<flow_id> {"host":"192.168.1.79",
      "device_class":"auto","port":5555,"more_options":{}}
```
`more_options: {}` ist Pflicht, sonst `required key not provided`. Beim
Absenden erscheint die ADB-Freigabe am Fernseher und muss dort bestätigt werden.
Neue Entity: `media_player.android_tv_192_168_2_79`.

**App-Start:** `androidtv.adb_command` mit
`monkey -p <paket> -c android.intent.category.LAUNCHER 1` — braucht den
Activity-Namen nicht. Alle sechs am Gerät verifiziert.

**Wichtig:** Die vorhandene, vom Nutzer deaktivierte `androidtv`-Integration
zeigt auf 192.168.1.41 (Sony-Fernseher). Für die Streamer-Apps ist sie nutzlos.

### 15. Bluetooth-Menü: zwei Fehler (2026-09-03)

**`on_touch` feuert während der ganzen Wischbewegung**, nicht nur beim
Antippen. Der Fokus wanderte dadurch unter dem Finger mit, und sobald er die
erste Zeile streifte, sprang die Seite nach ganz oben (wegen der Regel
„Zeile 0 = ganz nach oben scrollen"). Fix: Global `touch_active`, gesetzt beim
ersten Kontakt, zurückgesetzt in `on_release` — nur der erste Kontakt einer
Geste synchronisiert den Fokus.

**Auswahl und Fokus sahen gleich aus.** Beide benutzten die Fokusfarbe, dadurch
wirkte jedes Weiterblättern wie ein Verlust der Auswahl. Jetzt getrennt:
Fokus = blauer Rahmen, Auswahl = grüner Hintergrund (`0x14532D`, gesetzt in
`refresh_ble_slots` über `lv_obj_set_style_bg_color`). Dafür musste
`bg_color` aus dem `focused:`-Block der Slots raus, sonst überschreibt der
Fokus die Auswahlfarbe. Zusätzlich steht über den Aktionsknöpfen im Klartext
„Aktion für: N Name".

### 16. Mikrofon stumm: 1 V statt 3 V am VDD-Pad (2026-09-04)

**Symptom:** Nach einem Tag Betrieb lieferte das Mikrofon bitgenau `0x00000000`
(vorher `0x000003FF`), `peak 0` über zehntausende Samples, in **beiden**
Kanälen. Zwei verschiedene Module verhielten sich identisch.

**Ursache:** Eine hochohmige Lötstelle am **VDD-Pad (Pad 2)**. Am Chip kamen
nur **1 V** an statt 3 V; der MS3625 braucht mindestens 1,8 V. Er lief nicht
an, nahm den Takt aber entgegen und lieferte Stille.

**Warum das so lange gedauert hat — vier Messungen, die gesund aussahen:**
- **ESD-Klemmtest meldete VDD als verbunden.** Er arbeitet mit Mikroampere und
  ist von einigen Kiloohm Übergangswiderstand unbeeindruckt. Er prüft
  *Verbindung*, nicht *Spannungshöhe*.
- **Taktmessung war perfekt** (3175 SCK- zu 50 WS-Wechseln = 63,5 statt 64).
  Den Takt erzeugt der Prozessor; das Mikrofon braucht dafür keinen Strom.
- **Zweites Modul verhielt sich gleich** — was fälschlich gegen einen
  Modulfehler und für die Platine sprach, aber die Lötstelle nicht ausschloss.
- **D3 leuchtete.** Die LED hängt auf der *Platinenseite*, vor der schlechten
  Stelle.

**Methodische Lehren:**
1. **Bei Ausgängen erst messen, wenn kein Peripheriegerät die Pins hält.** Der
   erste Klemmtest lief 20 s nach dem Start, als I2S SCK und WS bereits trieb —
   `gpio_get_level()` gab den selbst getriebenen Ruhepegel zurück. WS las 0 und
   sah nach Kurzschluss aus. Der Test gehört in `on_boot` mit `priority: 900`.
2. **Logausgaben bei Priorität 900 erreichen niemanden** — das Netzwerk steht
   noch nicht. Ergebnis in ein Global puffern und später ausgeben, und die
   Ausgabe **wiederholen**: nach einer OTA braucht `esphome logs` bis zu 40 s
   zum Wiederverbinden.
3. **Eine wiederholte Ausgabe ist keine wiederholte Messung.** Drei identische
   Zeilen aus demselben Puffer wurden fälschlich als drei stabile Messungen
   gedeutet.
4. **Ein Messwert, der zu einem Fehler passt, passt oft auch zu einem gesunden
   Zustand.** Erst der kontrollierte Vergleich (mit Brücke / ohne Brücke) hat
   getragen.

### 17. Sprachqualität: Änderungen an der funktionierenden Kette (2026-09-04)

Nach der Reparatur wurden vier Dinge geändert, um die Erkennung zu verbessern:
Framing 256 → 320 Samples (50 statt 62,5 Pakete/s wie Firmware 3.56),
`gain` 6.5 → 3.0 → 4.0 → 10.0, `soft_limit` aus → ein, Knie 15000 → 24576.

**Ergebnis: Die Erkennung wurde schlechter** ("naja" → "viel schlechter" →
"schlecht"). Alles wurde auf den Stand von 2026-09-03 zurückgesetzt, danach war
sie wieder in Ordnung.

**Lehre:** Die Referenzfirmware ist ein anderes Gerät mit anderem Signalweg
(sie dezimiert 16 → 8 kHz und braucht deshalb Tiefpass und andere Pegel). Ihre
Konstanten sind Hinweise, keine Vorgaben. Und: nie vier Dinge gleichzeitig an
einer nachweislich funktionierenden Kette ändern. Falls die Erkennung später
doch verbessert werden soll, **genau eine** Änderung mit Rückmeldung dazwischen.

**Bestätigt funktionierender Stand:** `ATVV_FRAME_SAMPLES = 256`, `gain: 6.5`,
`soft_limit: false`, Knie 15000, `channel: left`, CAPS_RESP unverändert
(Byte [6] = 0x14 = 20, wie bei der mitgeschnittenen Sony- und Google-Remote).

### 18. Android TV lud bei jedem Verbinden das Bild neu (2026-09-04)

**Symptom:** Bei jeder BLE-Verbindung baute der Streamer (und der Fernseher)
seine Oberfläche neu auf — in YouTube sekundenlanges Nachladen. Mit den
Originalfernbedienungen passierte das nicht.

**Ursache:** Die Fernbedienung meldete sich als **vollwertige Tastatur**
(Report ID 1 plus Boot-Tastatur). Android wertet eine angeschlossene
Hardware-Tastatur als Konfigurationsänderung und startet die Oberfläche
laufender Apps neu — dieselbe Mechanik wie beim Drehen des Bildschirms.

**Lösung:** Zwei neue Optionen in `espidf_ble_keyboard`, beide für dieses Gerät
auf `false`:
- `pointer: false` — Maus und absoluter Zeiger fallen aus Gerätebeschreibung
  **und** GATT-Tabelle.
- `alpha_keyboard: false` — Tastatur-Report, Boot-Tastatur und der
  Ausgabe-Report fallen weg.

Wir brauchen beides nicht: D-Pad, OK, Zurück, Home, Lautstärke und Transport
laufen über die Medientasten-Seite (`consumer:0x00xx`).

**Belegt am Log.** Vorher abonnierte der Host `Boot KB input`, `Report input`,
`Consumer`, `Mouse`, `Abs mouse`. Nach der Umstellung und einer Neukopplung nur
noch `Consumer CCC` — und das Neuladen war weg.

**Wichtig:** Der Host cacht Gerätebeschreibung und Dienstaufbau **pro
Kopplung**. Nach jeder solchen Änderung muss auf **beiden** Seiten neu gekoppelt
werden — erst am Gerät „OpenRemote" entfernen, dann an der Remote löschen. Wird
nur eine Seite gelöscht, scheitert die Kopplung mit `Pairing Failed (0x66)`
bzw. `(0x56)`: Die Gegenseite verschlüsselt mit einem Schlüssel, den es hier
nicht mehr gibt.

**Zwischenschritt, der NICHT die Ursache war:** Die Maus allein zu entfernen
brachte nichts — nachgewiesen durch eine Kopplung, nach der `Mouse CCC`
verschwand, das Neuladen aber blieb.

### 19. Weitere Änderungen dieses Tages (2026-09-04)

- **Aktivitäten:** 0 = Google Streamer (BLE-Platz 1), 1 = TV (Platz 2). Der
  Wechsel schaltet den BLE-Host mit um; Knöpfe unten auf der Startseite zeigen
  die aktive Aktivität.
- **Akkustand über BLE:** `battery_level_val` stand fest auf 100 und wurde nie
  aktualisiert. Neue Methode `set_battery_level()`, gespeist vom max17048.
- **Statuszeile:** Bluetooth- und WLAN-Stärke als gezeichnete Balken (nicht als
  Schriftzeichen — die sitzen in Audiowide schief). Einzeln abschaltbar unter
  Einstellungen → Anzeige, ebenso Akku und Uhr.
- **Regler:** Fokusring nach AUSSEN war am Displayrand abgeschnitten und damit
  unsichtbar. Jetzt Auswahl innerhalb des Reglers (hellere Schiene, blauer
  Knopf), Breite 180 mittig.
- **Menü-Taste:** kurz = Menü auf/zu, gehalten (700 ms) = Menü-Startseite.
  `long_press` stand auf 7 s, als Bedienfunktion unbrauchbar.
- **Werbung:** Adresse lässt sich nur setzen, wenn keine Werbung läuft. Erst
  stoppen, im STOP-Ereignis neu starten. Notfrist beim Slot-Wechsel 400 ms.

**Merksatz aus diesem Tag:** Textersetzungen über Blockgrenzen hinweg haben
dreimal ganze Abschnitte gefressen (`button:`, der komplette `interval:`-Block,
`page_bright`). Nach jedem Ausschneiden gezielt prüfen, ob die kritischen Blöcke
noch da sind — `App.get_buttons`, `lift_cnt`, `idle_clock` und die Seiten-IDs.

### 20. Slider: `width:` im YAML kommt nicht an (2026-09-04)

**Symptom:** Regler ragten rechts aus dem Display, der senkrechte Regler auf der
Startseite erschien waagerecht und war trotz `hidden: true` nach dem Start
sichtbar.

**Ursache:** Bei ESPHomes LVGL-`slider` wirken `width:`, `height:` und
`hidden:` aus der Konfiguration nicht. Gemessen wurden **immer 260 px**,
unabhaengig vom eingetragenen Wert (216, 204, 180, 30 - alle wirkungslos). Und
weil 260 breiter als hoch ist, zeichnet LVGL den Regler waagerecht: Die
Ausrichtung ergibt sich aus dem Seitenverhaeltnis, nicht aus einer Option.

**Loesung:** Skript `fix_slider_geometry` setzt Groesse, Ausrichtung und das
Ausblenden per `lv_obj_set_width/height`, `lv_obj_align` und
`LV_OBJ_FLAG_HIDDEN` - aufgerufen im `on_load` **jeder** Seite mit Reglern
(22 Helligkeit, 13 Timeout, 14 Bewegung, 0 Startseite). Einmal beim Booten
genuegt nicht.

**Wie es gefunden wurde:** Vier Aenderungen an der Zahl blieben wirkungslos,
bevor ueberhaupt geprueft wurde, ob die Aenderung ankommt. Die Diagnose war ein
Intervall, das `lv_obj_get_coords` protokolliert und `lv_obj_update_layout`
selbst erzwingt - so haengt die Messung nicht daran, dass jemand die Seite
oeffnet.

**Merksatz:** Wenn dieselbe Korrektur mehrfach nichts bewirkt, nicht den Wert
weiter variieren, sondern pruefen, ob die Aenderung ihr Ziel erreicht.

### 21. Weiche Begrenzung: doch noetig, aber allein (2026-09-04, abends)

Abschnitt 17 hielt fest, dass das Einschalten der weichen Begrenzung die
Erkennung verschlechtert habe. **Das war eine Fehlzuordnung.** Damals wurden
vier Dinge gleichzeitig geaendert; die eigentliche Verschlechterung kam vom
gleichzeitig gesenkten `gain` (6.5 -> 4.0), also schlicht von zu leise.

**Belegt am Mitschnitt:** 19 von 58 Sprachabfragen wurden beschnitten, im
schlimmsten Fall zu **11,6 %** (Spitze 19612 roh). Ein Drittel aller
Aeusserungen - kein Randfall.

**Jetzt:** `soft_limit: true`, `gain: 6.5` unveraendert, Knie 15000.
Nachgemessen: Spitze 6500 x 6.5 = 42250 (haette gekappt) -> `clipped 0/93280`,
Mittelwert 487 gegen 478 vorher, also **keine Lautstaerke verloren**. Nutzer
bestaetigt die Erkennung als gut.

**Lehre:** Eine Aenderung, ein Messwert. Der erste Anlauf war nicht falsch
gedacht, nur nicht isoliert - und dadurch fuehrte er zur falschen Schluss-
folgerung, die dann monatelang in der Doku gestanden haette.

### 22. Zweite Fernbedienung `remote-wz` (2026-09-04)

Neues Geraet unter **192.168.1.60**, Konfiguration `remote-wz.yaml`. Enthaelt
jetzt einen eigenstaendigen **Mikrofontest** ohne BLE/ATVV: Klemmtest der drei
Signalleitungen bei `on_boot` priority 900 (gepuffert, alle 30 s ausgegeben)
plus Dauer-Aufnahme ueber `microphone.capture` mit Pegelanzeige im Sekundentakt.

**Erster Befund:** SD und SCK werden bei abgeschalteter Versorgung geklemmt,
**WS nicht** (`pullup=1`). Dazu fehlt die Bruecke L/R (Pad 6) auf Masse. Die
fehlende Bruecke erklaert die Stille vollstaendig, aber NICHT das WS-Verhalten -
die Klemmung laeuft ueber passive Schutzdioden und haengt nicht am L/R-Pegel.
Also beides pruefen: erst Bruecke, dann neu messen, dann ggf. WS nachloeten.

### 23. Eigenstaendiger Mikrofon-Kontakttest (2026-09-05)

Der Test steckte bisher in `remote-wz.yaml`, also in der Konfiguration der
zweiten Fernbedienung. Damit war er nur dort brauchbar. Jetzt liegt er als
`mikrofon-kontakttest.yaml` eigenstaendig vor, mit eigenem Geraetenamen
`mikrofon-kontakttest`, eigenem API-Schluessel und eigenem AP-Passwort
(`api_kontakttest_key`, `ap_kontakttest_password` in `secrets.yaml`).

Die Pinbelegung steht oben in `substitutions:` als nackte Nummer - der
elektrische Test braucht sie als Zahl, `GPIO45` laesst sich in einem Lambda
nicht verwenden. Fuer andere Hardware also nur diesen Block anfassen.

**Eigene Entitaetsnamen waren noetig.** Die Kennungen in Home Assistant lauten
`MAC/0/domain/Name`. Haette der Test dieselben Namen wie `remote-wz` benutzt,
waeren die neuen Entitaeten auf derselben Platine stillschweigend blockiert
worden - genau der Fehler, der bei "Sprachsuche 4s" schon einmal Stunden
gekostet hat.

Beim ersten Aufspielen auf eine Platine, die noch anders heisst, muss
`use_address:` im `wifi:`-Block auf deren IP zeigen, sonst sucht ESPHome nach
`mikrofon-kontakttest.local` und findet nichts.

### 24. Ladeanzeige (2026-09-05)

Der MAX17048 fuehrt in **Register 0x16 (CRATE)** die Lade- bzw. Entladerate in
0,208 % pro Stunde je Schritt, mit Vorzeichen. ESPHome legt den Wert nicht
offen, also liest ihn das Skript `refresh_laden` direkt vom Bus - dafuer hat
der I2C-Bus jetzt die Kennung `hauptbus`.

Positiv heisst: der Akku fuellt sich. Die Statuszeile zeigt dann ein
vorangestelltes **+** und faerbt den Wert gruen (0x22C55E), sonst bleibt er
grau. Bewusst **kein** Vergleich zweier Messpunkte ueber die Zeit - der
braeuchte Minuten und wuerde bei jedem Rundungssprung flackern. Totbereich
0,5 %/h, damit Rauschen um die Null nicht hin- und herschaltet.

### 25. Musik ueber die Multiroom-Gruppe (2026-09-05)

Die Musikseite hatte fuenf Knoepfe, die je ein Google-Skript starteten. Neu ist
der Weg ueber **Music Assistant**: `media_player.multiroom_audio` ist ein
Gruppenspieler, dessen Mitglieder sich zur Laufzeit aendern lassen
(`supported_features` enthaelt GROUPING).

- **Seite 30 "Lautsprecher"** listet sieben Spieler mit ihrem Zustand
  (gruen "dabei" / grau "aus"). Ein Druck nimmt sie ueber `media_player.join`
  auf oder wirft sie ueber `media_player.unjoin` heraus. Beim Aufnehmen ist das
  Ziel die Gruppe, beim Entfernen der Lautsprecher selbst - deshalb zwei
  getrennte Dienstaufrufe. Dazu "Alle dazu" und "Alle raus".
- **Seite 31 "Musik ueber Google"** enthaelt die fuenf alten Skript-Knoepfe
  unveraendert.
- Die Musikseite selbst zeigt nur noch "Lautsprecher (x von 8)", Play/Pause,
  Aus, "Musik ueber Google" und die beiden Video-Knoepfe.

**Die Mitgliederliste kommt als Attribut** `group_members` von
`media_player.multiroom_audio`. Home Assistant reicht Attribute als Python-
Schreibweise durch, also `['media_player.a', 'media_player.b']`. Gesucht wird
deshalb samt der einfachen Anfuehrungszeichen - sonst wuerde eine Kennung, die
zufaellig am Anfang einer anderen steckt, faelschlich als Treffer zaehlen.
Die Tabelle der Spieler steht in `esphomeremote-musik.h`, weil sie an drei
Stellen gebraucht wird und beim Abschreiben eine Kopie vergessen worden waere.

**Ueberlaufende Zeile behoben:** "Wiedergabe laeuft in" und der Raumname stehen
jetzt untereinander statt nebeneinander. In einer Zeile lief schon "Bad" ueber
den Rand, weil das Label auf 118 px festgenagelt war.

**Nachgemessen statt angenommen (2026-09-05).** Der erste Entwurf hatte acht
Lautsprecher, darunter Tablet Flur. Music Assistant lehnt das ab und schreibt
es selbst ins Protokoll:

    Player I01RUkMtED8... can not be grouped with Multiroom Audio

Music Assistant fasst nur Geraete derselben Art zusammen. Tablet Flur steht
deshalb bewusst nicht mehr in der Liste - ein Knopf dafuer haette nur einen
Fehlschlag ausgeloest. `media_player.unjoin` auf ein Nicht-Mitglied antwortet
mit HTTP 500, das war der zweite Hinweis.

Beide Richtungen sind an einem echten Mitglied geprueft: Beitritt 3 -> 4,
Verlassen 4 -> 3, beide HTTP 200. Und die Fernbedienung sieht das auch:

    [D][musik]: Gruppenmitglieder roh: ['media_player.badezimmer_lautsprecher',
    'media_player.tablet_wohnzimmer_3', ...]

Damit ist auch das Format geklaert - Python-Schreibweise mit einfachen
Anfuehrungszeichen. Die Erkennung in `esphomeremote-musik.h` kommt trotzdem ohne
diese Annahme aus und sucht die nackte Kennung mit Grenzpruefung, falls Home
Assistant das eines Tages als JSON schickt.

**Ein Tablet faellt von selbst wieder aus der Gruppe**, ein echter Lautsprecher
nicht. Tablet Kueche war nach 22 s wieder draussen, Kuechen Lautsprecher L nach
60 s noch drin. Die erste Vermutung "Music Assistant verwirft Mitglieder einer
leeren Gruppe" war also falsch - es liegt an den Tablets, die im Ruhezustand
offline gehen.

**Play bei leerer Warteschlange tut nichts.** Im Protokoll steht dann
`players/cmd/play: Resume queue requested but queue Multiroom Audio is empty`.
Erst Musik starten, dann Lautsprecher waehlen.

### 26. Stufe 2 des Stromsparens brach den Home-Assistant-Assistenten (2026-09-05)

Symptom: Google reagiert, Home Assistant nicht - die Sprachseite ging gar nicht
auf. Ursache war nicht der Assistent, sondern das Netz: Stufe 2 "WLAN aus im
Ruhezustand" schaltet den Funk ab, sobald das Display schlaeft. Google laeuft
ueber Bluetooth und merkt davon nichts, Home Assistant laeuft ueber die API -
und ohne WLAN gibt es keine API. In Home Assistant standen alle Entitaeten der
Fernbedienung auf "nicht verfuegbar", `WLAN verbunden` auf `aus`, und sie
antwortete nicht einmal auf Ping.

Dazu ein zweiter Fehler: `voice_start` rief `wake_backlight` nur im
**Google-Zweig** auf. Im Home-Assistant-Zweig fehlte das Wecken, also blieb das
WLAN auch dann aus, wenn das Aufwachen es wieder eingeschaltet haette.

Behoben dreifach:
- Stufe 2 ruht, solange Home Assistant als Assistent gewaehlt ist. Auf der
  Sparseite steht dann orange "ruht" statt "AN" - sonst sucht man den Fehler
  beim Stromsparen.
- Die Sprachtaste weckt jetzt in beiden Zweigen zuerst.
- Fehlt die Verbindung trotzdem, wartet sie bis zu 8 s auf `api.connected` und
  schreibt sonst "Home Assistant ist nicht erreichbar" aufs Display, statt
  still nichts zu tun.

### 27. Geraetenamen aufgeraeumt (2026-09-05)

Bis hierher hiess die taegliche Fernbedienung noch nach ihrem Testursprung
`esphomeremote-t5` / "Open Remote Test 5 (Menu UI)". Neu:

| Platine | vorher | jetzt |
|---|---|---|
| 192.168.1.69 (taeglich) | `esphomeremote-t5` | `remote-wz-wohnzimmer` / **Remote WZ Remote 2** |
| 192.168.1.60 (zweite)   | `remote-wz`      | `remote-wz-couch` / **Remote WZ Remote 1** |

Die AP-Rueckfallnetze heissen entsprechend mit.

**Ein Umbenennen in ESPHome aendert die Entitaets-IDs in Home Assistant
NICHT.** Die IDs entstehen einmalig beim Anlegen; danach passt Home Assistant
nur noch den Anzeigenamen an. Nach dem Flashen stand also
`sensor.open_remote_test_5_menu_ui_akkustand` mit dem Anzeigenamen
"Remote WZ Remote 2 Akkustand" da - ein halber Umzug. Die 21 Entitaeten wurden
deshalb ueber `config/entity_registry/update` (Websocket) auf
`*.remote_wz_wohnzimmer_*` nachgezogen.

Vorher geprueft, ob das etwas bricht: der alte Name kam ausserhalb der
Registry nur in `configuration.yaml` vor, in keinem Dashboard und keiner
Automatisierung. **Der Recorder-Eintrag musste mitgezogen werden** - sonst
haette die Akkuaufzeichnung ab dem Umbenennen stillschweigend aufgehoert. Nach
dem Neustart bestaetigt: 14 Punkte unter `sensor.remote_wz_wohnzimmer_akkustand`.

Die zweite Platine ist in `remote-wz.yaml` umbenannt, aber **noch nicht
geflasht** - dort laeuft weiterhin der Mikrofon-Kontakttest. Sie heisst in Home
Assistant bis zum naechsten Flashen weiter "Remote WZ".

### 28. Tiefschlaf im Ruhezustand (2026-09-06)

**Ausloeser:** Akku ueber Nacht leer trotz Sparstufen 1+2. Messung
(`sensor.remote_wz_wohnzimmer_akkustand`, Historie 05.09.): abends ~3,3 %/h, nachts
mit abgeschaltetem WLAN (Stufe 2 hat ab 22:16 nachweislich gegriffen -
Geraet "unavailable") sogar ~6,3 %/h. Die Fuel-Gauge-Kennlinie ist zwischen
80-40 % flach, real war der Verbrauch also durchgehend ~100-120 mA. Grund: es
schlaeft nichts wirklich - `light_sleep_enable = false`, Tickless-Idle bewusst
aus, BLE-Controller + Octal-PSRAM + Display-Modul dauerhaft versorgt.
`wifi.disable` legt den Funkteil unter BT-Koexistenz nicht sauber schlafen.

**Neu:** vierter Schalter unter *Einstellungen -> Energie sparen* -> "Tiefschlaf
im Ruhezustand" (`id(ps_deep)`, `restore_value`, **Standard AUS**). Bewusst
kein Eingriff in den Wach-Zustand (der ATVV-Audiopfad bleibt Byte fuer Byte
unangetastet) - Deep-Sleep ist ein voellig getrennter Codepfad.

- Neue Komponente `deep_sleep: id(nachtschlaf)`, **kein** `run_duration` -
  schlaeft nur, wenn ein Script `deep_sleep.enter` ruft.
- `backlight_idle_timeout` legt den Chip 90 s nach dem Display-Aus schlafen -
  aber nur wenn `ps_deep` an **und** nicht am Kabel (`!laedt`, direkt aus dem
  CRATE-Register), keine Sprachabfrage (`!voice_active && !host_mic`) und der
  Fernseher aus (`!tv_on`).
- Weckquellen: `wakeup_pin: GPIO8` (TCA8418-INT, aktiv LOW, daher `inverted`) =
  jeder Tastendruck; plus `sleep_duration: 20min` als Ruecksicherung, falls der
  Weckpin auf der Hardware nicht zieht.
- Wecken per Timer: `woke_timer` (aus `esp_sleep_get_wakeup_cause()` in on_boot
  prio 900) -> `timer_nickerchen` haelt das Geraet ~25 s online (Akkustand an
  HA, OTA-Fenster) und schlaeft dann ohne Display wieder ein. Ein Tastendruck
  in der Zeit -> `wake_backlight` -> `script.stop: timer_nickerchen` -> wach.
- `ota: on_begin` stoppt beide Sleep-Scripts, setzt `ota_aktiv` und ruft
  `deep_sleep.prevent`; `on_error` raeumt das wieder auf.
- `esphomeremote-pm.h` bringt jetzt zusaetzlich `esp_sleep.h` mit.

**Fernwartung / Flashen, wenn niemand das Geraet wecken kann:**
- **Wartungsfenster:** ein 30-min-`interval` ruft bei ausgeschaltetem Display
  das Script `wartungsfenster` -> WLAN 5 min erzwungen an (`id(wartung_bis)` =
  `millis() + 5min`). Die `wifi.disable`-Zeile in `backlight_idle_timeout` und
  alle Tiefschlaf-Bedingungen pruefen jetzt zusaetzlich
  `(int32_t)(millis() - wartung_bis) >= 0 && !ota_aktiv`. So ist die
  Fernbedienung mit Stufe 2 spaetestens alle 30 min ~5 min per OTA erreichbar.
- Im **Tiefschlaf** laeuft kein `interval` -> dort ist `sleep_duration` auf
  **15 min** und `timer_nickerchen` haelt **60 s** offen (statt 25 s), lang
  genug, damit ein OTA anlaufen kann; danach uebernimmt `ota on_begin`.
- **HA-Schalter** `switch.remote_wz_wohnzimmer_tiefschlaf_im_ruhezustand` (Template,
  spiegelt `id(ps_deep)`) - aus = Geraet bleibt online, an = Akku schonen.
- **HA-Button** "Wartungsfenster jetzt" (`button ... on_press: wartungsfenster`)
  fuer sofort statt bis zu 30 min warten.
- Erst-Flash (altes Image ohne all das): Akku laden, dann im ~45-s-Bootfenster
  per OTA erwischen; Smart-Stecker `switch.ambilight_wz_steckdose_alt` sitzt
  zwischen Netz und Remote-Ladegeraet und laesst sich zum Power-Cycle nutzen.

**Bewusste Nachteile:** erster Tastendruck nach dem Einschlafen weckt nur
(Reconnect ~3 s), erst der zweite loest aus. Deshalb Standard AUS und der Hinweis
auf der Sparseite. Bewegung weckt NICHT aus dem Tiefschlaf (nur GPIO8 + Timer) -
das war vorher ein Verdaechtiger fuers naechtliche Wachbleiben.

**Auf Hardware geflasht 06.09.2026** (per OTA, `esphome upload --file`). `deep_sleep`
mit `wakeup_pin` GPIO8 kompiliert auf dem S3 - Pin-Konflikt mit dem tca8418-
Interrupt via `allow_other_uses: true` auf beiden Pin-Definitionen geloest.
Boot-Log bestaetigt: `deep_sleep: Sleep Duration 900000 ms | Wakeup Pin GPIO8`.
Noch offen: ob ein Tastendruck wirklich aus dem Tiefschlaf weckt (Testmodus erst
ausschalten), und Verbrauchsmessung im Tiefschlaf gegen die Historie.

### 29. Sicherheitsnetz fuers Ferntesten (2026-09-06)

Remote 2 ist laenger weg, Claude soll autonom testen und flashen koennen, ohne sich
je auszusperren ("wenn deepsleep dann kein WLAN und kein Flashen").

- **`switch.ambilight_wz_steckdose_alt`** ist die Philips-Hue-Steckdose mit dem
  Remote-Ladegeraet (nicht die gleichnamige Shelly-Leiste `..._steckdose`!).
  Solange die an ist, laedt die Remote und der Akku kann nicht sterben.
  `power_on_behavior` auf "on" gesetzt.
- **`id(testmodus)`** (global, restore, **Standard AN**) - HA-Schalter
  `switch.remote_wz_wohnzimmer_testmodus_immer_online` + 5. Toggle auf der Sparseite.
  AN = `wifi.disable` (Stufe 2) und `deep_sleep.enter` werden komplett
  uebersprungen. Fuer Akku-Messungen ausschalten.
- **`id(boot_grace)`** - 10 min nach jedem *echten* Boot (Kaltstart / Reset /
  OTA-Reboot) kein Tiefschlaf / kein WLAN-Aus. Nach Aufwachen aus dem Tiefschlaf
  in `on_boot` prio 900 sofort auf false (Weckgrund `ESP_SLEEP_WAKEUP_TIMER/
  GPIO/EXT*`), sonst kaeme der Chip nie zum Schlafen. `boot_grace_timer` setzt
  nach 600 s zurueck und ruft `backlight_idle_timeout` erneut.
- **`id(api_ever)`** (`api: on_client_connected`) + **`api.connected:`** in den
  Tiefschlaf-Bedingungen: es wird nur geschlafen, wenn HA in diesem Wachfenster
  auch wirklich erreichbar war/ist. `timer_nickerchen` hat einen Deadman: ist
  die API nach den 60 s nicht verbunden -> `wake_backlight`, Geraet bleibt wach
  und flashbar, statt blind weiterzuschlafen.
- **Stundenraster:** `time: on_time: minutes: 0` -> `wartungsfenster`
  (zusaetzlich zum 30-min-`interval`). Zur vollen Stunde ist also garantiert
  ein 5-min-Fenster.
- **Ladegeraet-Flanke:** `refresh_laden` ruft bei `laedt` false->true
  `wake_backlight` + `wartungsfenster`. "Ans Kabel legen" holt die Remote also
  zuverlaessig online.

**Flash-Weg von hier aus** (SSH-Addon `a0d7b954_ssh`): `esphome` ist per pip
installiert (`~/.local/bin`, v2026.8.2). `esphome compile esphomeremote-test-5-ui.yaml`,
dann `esphome upload ... --device 192.168.1.69 --file .esphome/build/remote-wz-wohnzimmer/build/firmware.ota.bin`
(`--file` = kein Recompile). Build-Cache unter `/homeassistant/esphome/.esphome/`
(gitignored, ~800 MB). `secrets.yaml` hat den OTA-/API-Kram.

**Stand nach 6 OTA-Flashes am 06.09.:** Firmware laeuft stabil, alle neuen
HA-Entities da, BLE+WLAN verbunden, Akku laedt (Hue-Steckdose an).
`testmodus` bootet zuverlaessig auf AN (Template-Schalter `restore_mode: DISABLED`
+ Global `restore_value:true` + Zwangssetzung in on_boot prio 100, aber nur wenn
`boot_grace` - also nicht nach einem Deep-Sleep-Wake).

### 30. Stromspar-Messung + Zwischenstufe BLE-Idle / CPU-80 (2026-09-06, v7-v10)

Vollmessung: **`esphomeremote-docs/esphomeremote-stromspar-messung.md`**. Kurz:

| Konfig (Display aus) | Rate | Laufzeit ab 100 % |
|---|---|---|
| WLAN an, 240 MHz, BLE 15 ms/Lat 0 | −23 %/h | ~4,5 h |
| + BLE-Idle (150 ms/Lat 3) | −12 %/h | ~8 h |
| + CPU 80 MHz | −7,5 %/h | ~13 h |
| + WLAN aus (Stufe 2) | −3,2 %/h | ~30 h |
| Deep-Sleep (Peripherie noch an) | −1,1 %/h (~22 mA) | ~3-4 Tage |

**Nächtlicher Killer = BLE mit Latency 0** (Funk wacht 66×/s, blockiert
WLAN-Koexistenz → ~460 mA trotz dunklem Display). Alte Sparstufen schnitten nur
WLAN ab.

Neu in v7-v10:
- **`request_conn_profile(bool idle)`** im `espidf_ble_keyboard`-Component
  (Header + .cpp). idle = int 0x60-0x78 / latency 3 / timeout 600, aktiv =
  0x06-0x0C / 0 / 400. Aus `backlight_idle_timeout` (~11 s nach Display-aus,
  `!voice_active && !host_mic`) und `wake_backlight`. Streamer akzeptiert Lat 3
  (`GAP: Conn params updated int=108 latency=3 timeout=600` verifiziert).
  Global `ps_bleidle`, Standard **AN**.
- **`apply_cpu`**-Script: 80 MHz wenn `ps_cpu && disp_off && !ota_aktiv`, sonst
  240. Aus apply_power / backlight_idle_timeout / wake_backlight. `ps_cpu`
  Standard **AN** (macht jetzt 80 statt 160, nur bei Display aus).
- HA-Schalter fuer alle Sparoptionen (`switch.remote_wz_wohnzimmer_sparen_*`) +
  `sensor.remote_wz_wohnzimmer_akku_rate` (CRATE %/h, `force_update`) als Messinstrument.
- **Deep-Sleep-Persistenz:** `RTC_DATA_ATTR uint32_t g_deep_active` in pm.h
  (0x5EEE). Wird bei tiefschlaf-AN / `deep_sleep.enter` gesetzt, on_boot prio 900
  bei Deep-Sleep-Wake wieder `ps_deep = true`. Der `restore_value`-Global ueberlebt
  den `deep_sleep.enter` NICHT zuverlaessig (Flash-Write evtl. zu spaet) - in
  Phase E kippte ps_deep nach einem Wake auf false und das Geraet blieb wach.

**NOCH ZU PRUEFEN / OFFEN:**
1. GPIO8-Weckpin aus Deep-Sleep physisch (Tastendruck) - fern nicht testbar.
   Timer-Wake alle 15 min laeuft.
2. **Peripherie vor Deep-Sleep abschalten** fuer 22 mA → <1 mA: Panel-Rail
   (`enable_pin` GPIO38), Backlight-PWM detach (GPIO9/46), Datenbus (GPIO
   48/47/21/14/13/12/11/10) + Steuerpins LOW parken gegen Back-Feed,
   SD_EN GPIO16 HIGH, MIC GPIO45 LOW, `gpio_hold_en` + `gpio_deep_sleep_hold_en`,
   Touch/LIS3DH in Low-Power. Wie die Referenz (`neutraliseRev5DisplayBusForDeepSleep`
   etc.). RISKANT ohne physische Kontrolle (Ghost-Display, Boot) - fuer Remote 2' Rueckkehr.
3. g_deep_active-Persistenz-Test laeuft (dstest.log).
