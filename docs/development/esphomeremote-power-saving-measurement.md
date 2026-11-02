# Stromspar-Messung Open Remote (remote-wz-wohnzimmer) — 2026-09-06

Autonom gemessen, Remote 2 war nicht da. Akku 103450 LiPo, 3,7 V / ~2000 mAh.
Bedingungen: Display aus, Fernseher aus, BLE mit dem Google Streamer verbunden,
Ladegeraet ab, SOC-Bereich ~78-95 % (Fuel-Gauge dort einigermassen linear).
Messung ueber CRATE-Register (0x16, %/h) im Debug-Log + SOC-Drift aus der
HA-Historie. Beide Methoden stimmen ueberein.

## Ergebnisse

| # | Konfiguration (Display aus) | Rate | ≈ Strom | Laufzeit ab 100 % |
|---|---|---|---|---|
| **A** | WLAN an (light PS), CPU 240 MHz, BLE 15 ms / Latency 0 | **−23 %/h** | ~460 mA | **~4,5 h** |
| **B** | A + **BLE-Idle-Profil** (150 ms / Latency 3) | **−12 %/h** | ~250 mA | ~8 h |
| **C** | B + **CPU 80 MHz** | **−7,5 %/h** | ~150 mA | ~13 h |
| **D** | C + **WLAN aus im Ruhezustand** (Stufe 2) | **−3,7 %/h** rein, **−4,5 %/h** mit Wartungsfenstern | ~75-90 mA | **~22-27 h** |
| **E** | Deep-Sleep (v13: keine Peripherie-Abschaltung, 6 h Wake, kein phone-home) | **wird gemessen** (dstest4, ~4 h sauber) | ~? mA | ~? Tage |

> **Deep-Sleep-Messungen sind schwierig:** die MAX17048-Fuel-Gauge ist
> spannungsbasiert und springt unter dem gepulsten Last-Muster (Sleep + Wake-
> Zyklen) - kurze Fenster gaben -1 bis -8 %/h. Nur ein LANGES Fenster mit
> wenigen Wakes (dstest4, 6 h sleep_duration) gibt eine belastbare Zahl.
>
> **Panel-Rail-Abschaltung (GPIO38) - fehlgeschlagen (06.09.):** Versuch v11
> (Panel per SPI-SLPIN) und v12 (Rail + Datenbus per gpio_hold parken, nach
> OMOTE-Rezept) haben den Verbrauch **verschlechtert** (~-6 bis -8 %/h). Ursache
> vermutlich Pull-ups auf dem Datenbus (gehaltene LOW-Pins ziehen Strom) und/oder
> GPIO38 floatet ohne Board-Pullup. **Ohne Multimeter am Board nicht sauber zu
> loesen** - Code in git-Historie (v12). Bench-Aufgaben: Deep-Sleep-Strom mit
> Panel-Stecker ab/dran messen; Schaltplan GPIO38-Load-Switch + Datenbus-Pullups.

## Was das nächtliche Leerlaufen erklärt

Der grosse Verbraucher ist **BLE mit Latency 0 / 15 ms**: der Funk wacht 66×/s
auf und blockiert dabei die WLAN-Koexistenz (eine Antenne fuer beides). Ergebnis
~460 mA, obwohl das Display aus ist. Die alten "Sparstufen 1+2" haben nur das
WLAN abgeschaltet — den BLE-Dauerbeschuss nicht.

## Die drei wirksamen Hebel (jeweils bei Display aus)

1. **BLE-Idle-Profil** — Verbindungsintervall 150 ms, **Slave-Latency 3**,
   Supervision 6 s. Der Peripheral darf bis zu 3 Connection-Events auslassen,
   ein Tastendruck geht trotzdem beim naechsten Event raus → **kein Reconnect,
   keine spuerbare Verzoegerung**. Streamer akzeptiert es
   (`GAP: Conn params updated status=0 int=108 latency=3 timeout=600` verifiziert).
   **Spart ~11 %/h. Kein Nachteil.** → Standard AN (v10).
2. **CPU 80 MHz** bei Display aus (statt 240). Referenz-Firmware laeuft BLE-HID
   nachweislich stabil bei 80 MHz; der BT-Treiber holt sich Funktakt selbst.
   **Spart ~4,5 %/h. Kein Nachteil.** → Standard AN (v10).
3. **WLAN aus im Ruhezustand** (Stufe 2). **Spart nochmal ~4 %/h.**
   Nachteil: waehrend das Display aus ist, kein Home Assistant / kein OTA -
   nur alle 30 min + zur vollen Stunde je 5 min (Wartungsfenster), plus
   "ans Ladekabel legen" weckt sofort. → Standard AUS, aber empfohlen sobald
   `Testmodus` aus ist.

## Empfehlung

- **Alltag (Testmodus aus, Stufe 2 an):** ~−3,2 %/h → aus vollem Akku **~30 h**.
  Fuer eine Fernbedienung, die abends ein paar Mal benutzt und dann geladen
  wird, ist das mehr als genug. Das naechtliche Leerlaufen ist damit weg.
- **Testmodus an (Standard):** ~−7,5 %/h → ~13 h. Geraet bleibt immer per WLAN
  erreichbar/flashbar. Fuers Entwickeln.
- **Deep-Sleep:** aktuell **nicht empfehlenswert** - bringt kaum mehr als
  Stufe D (~1-2 Tage), weil Peripherie im Schlaf versorgt bleibt UND jeder
  15-min-Timer-Wake ~90 s bei vollem Strom kostet. Dazu die Reconnect-Strafe
  beim ersten Tastendruck. Erst sinnvoll mit Peripherie-Abschaltung (s.u.).
  Persistenz ueber Wake-Zyklen ist gefixt und **verifiziert** (v10, RTC-Flag
  `g_deep_active` - der ESPHome-restore-Global ueberlebt `deep_sleep.enter`
  nicht zuverlaessig; Test 06.09. 12:12-13:00: Geraet schlief, wachte ~12:42
  per Timer, `tiefschlaf` blieb AN, schlief wieder ein).

## Offen / nächste Schritte

1. **Peripherie vor Deep-Sleep abschalten** (wie die Referenz:
   `neutraliseRev5DisplayBusForDeepSleep`, `isolateRev5I2cBus`, `gpio_hold_en`
   auf die Power-Pins). Ziel: Deep-Sleep von 22 mA → <1 mA (Wochen). Braucht
   die Rev6-Pin-Zuordnung.
2. **GPIO8-Weckpin aus dem Deep-Sleep** noch physisch pruefen (Tastendruck) -
   fern nicht testbar. Timer-Wake alle 15 min funktioniert.
3. Feinmessung Stufe 2 + Deep-Sleep ueber mehrere Stunden statt ~30-60 min.

## Firmware-Stand nach der Messung (v10)

- `switch.remote_wz_wohnzimmer_ble_verbindung_verlangsamen` (Standard AN)
- `switch.remote_wz_wohnzimmer_sparen_prozessor_drosseln` (Standard AN)
- `switch.remote_wz_wohnzimmer_sparen_wlan_aus_im_ruhezustand` (Standard AUS)
- `switch.remote_wz_wohnzimmer_sparen_wlan_sparmodus` (Standard AN)
- `switch.remote_wz_wohnzimmer_tiefschlaf_im_ruhezustand` (Standard AUS)
- `switch.remote_wz_wohnzimmer_testmodus_immer_online` (Standard AN — Sicherheitsnetz)
- `sensor.remote_wz_wohnzimmer_akku_rate` (%/h, Messinstrument)

---

## KORREKTUR 07.09. — Rev6-Schaltplan analysiert (OpenRemote-Hardware Repo)

Der echte Rev6-Schaltplan (`PCB/Revision 6/OpenRemote.kicad_pcb`) klaert die
Peripherie-Frage endgueltig. **Meine bisherige Annahme war falsch.**

### Power-Architektur Rev6
- `+3.3V` = Dauerstrom (LDO U2 XC6220, ~10 uA Ruhestrom)
- `+VSW`  = `+3.3V` geschaltet ueber **Q4 (P-MOSFET-Load-Switch)**
- Q4-Gate: Pull-up **R26 (10k) auf +3.3V**, angesteuert von **GPIO38 (LCD_EN)** ueber R28 (1k)
  - GPIO38 LOW  -> Gate ~0 V -> Q4 EIN  -> +VSW = 3.3 V
  - GPIO38 HIGH -> Gate 3.3 V -> Q4 AUS -> +VSW aus
  - **GPIO38 Hi-Z (Deep-Sleep, nicht gehalten) -> R26 zieht Gate hoch -> Q4 AUS -> +VSW aus**

### An +VSW haengen (werden im Deep-Sleep automatisch stromlos)
- Panel ILI9341 (J2)
- Touch-Controller (J6 Pin 5)
- **I2C-Pullups R11/R12 (4k7)** haengen auf +VSW -> sterben mit dem Touch
  (genau der Schaltplan-Hinweis "i2c pull-ups must be disabled in sleep")
- APX803S Reset-Supervisor (U10), microSD ueber SD_3V3/Q7 (analog)

### An +3.3V (Dauerstrom, IMMER versorgt)
- **TCA8418 Keypad (VCC -> +3.3V)**  -> Wecken GPIO8 bleibt
- **LIS3DH Accel (VDD + VDD_IO -> +3.3V)** -> Wecken GPIO2 bleibt
  (LIS3DH ist per CTRL_REG1=0x3F schon Low-Power, ~4 uA)
- MAX17048 (auf +BATT)
- TCA_INT (GPIO8): externer Pull-up **R36 (10k) auf +3.3V** -> funktioniert unabhaengig von +VSW

### Konsequenzen
1. **Der Deep-Sleep kappt +VSW bereits von selbst** - GPIO38 floatet, R26 macht Q4 aus.
   Panel + Touch + deren Pullups sind in JEDEM Deep-Sleep schon stromlos.
   Es war nie noetig, GPIO38 per Firmware zu treiben oder zu halten.
2. **Meine Abstuerze (v11/v12/Diag v14)** kamen daher, dass ich GPIO38 im
   LAUFENDEN Betrieb auf HIGH gezogen habe, WAEHREND die ft63x6-Touch-Komponente
   noch alle ~16 ms I2C pollte. Ohne Pullups (+VSW weg) haengt jede Transaktion
   -> Loop blockiert -> Absturz. Der sichere Weg (v16 `diag_rail_sim`: erst
   `component.suspend: main_touch` + `lvgl.pause`) lief problemlos.
3. `deep_sleep_powerdown` muss fast NICHTS tun - die Hardware macht das. Nur
   Touch-Komponente kurz stoppen, damit sie in den ~1 s vor dem Sleep nicht
   auf den sterbenden Bus pollt. KEIN gpio_hold, KEIN Bus-Parken (das war
   kontraproduktiv - Strom gegen die Pull-up-Widerstaende).
4. **Theoretischer Deep-Sleep-Boden:** ESP32-S3 (~10-100 uA) + TCA8418 (~uA) +
   LIS3DH (~4 uA) + MAX17048 (~23 uA) + LDO (~10 uA) + Batterieschutz (~uA)
   ~= 200-500 uA -> 2000 mAh / 0,35 mA ~= mehrere Monate (vor Wake-Zyklen).
   Mit 6-h-Wakes a ~60 s: +~0,4 mA -> **~2-4 Monate**.
   Die frueheren -1 bis -8 %/h waren Fuel-Gauge-Rauschen + zu haeufige Wakes.
5. **kein Loeten noetig.** GPIO2 (Bewegung) sollte noch als zweite Weckquelle
   dazu (ext1 ANY_LOW, LIS3DH-INT1 auf aktiv-LOW) - "hochheben = an".

### Noch offen
- GPIO8/GPIO2-Wecken physisch bestaetigen (ein Tastendruck / Anheben).

---

## ERGEBNIS 08.09. — v20..v23, voller Rev6-Deep-Sleep

### Firmware
`deep_sleep_powerdown` ist jetzt **synchron** (kein `- delay:`, blockierendes
`delay()` in der Lambda) -> laeuft komplett durch, BEVOR `deep_sleep.enter`; zwischen
"Rail aus" und `esp_deep_sleep_start()` kein Component-loop() -> kein I2C-Hang mehr.

Ablauf vor dem Schlaf (nach OMOTE-Referenz `enterSleep()`):
1. LIS3DH -> Low-Power (CTRL_REG1=0x2F, 10 Hz) + INT1 aktiv-LOW (CTRL_REG6=0x02)
2. `wifi.disable`, Mikrofon (GPIO45) aus, Touch-Komponente suspend
3. LCD-Ctrl/Daten-Pins LOW -> **GPIO38 HIGH** (Q4 aus -> +VSW/Display/Touch/
   I2C-Pullups R11/R12 stromlos). Ab hier KEIN I2C mehr.
4. GPIO9 (Q3 Backlight), GPIO5 (Q5 IR-TX), GPIO16 (Q7 microSD), GPIO6 (IR-RX) aus;
   GPIO4/19/20 hochohmig (kein Backfeed in die tote +VSW-Rail)
5. `gpio_hold_en` auf 38/9/16/6/5 + `gpio_deep_sleep_hold_en()` (GPIO46 NICHT -
   Strapping-Pin)
6. `esp_sleep_enable_ext1_wakeup((1<<8)|(1<<2)|(1<<1), ANY_LOW)` + interner
   RTC-Pull-up auf GPIO1
7. `delay(80)` -> `esp_deep_sleep_start()`

Wake: `on_boot` Prio 950 loest ALLE Holds + `rtc_gpio_deinit` -> dann Display-
Component faehrt die +VSW-Rail hoch (GPIO38-Flanke + 300 ms) -> dann Touch/WLAN/BLE.

### Weckquellen (alle RTC-faehig, GPIO0-21, ext1 ANY_LOW)
| Pin | Quelle | verifiziert |
|-----|--------|-------------|
| GPIO8 | TCA8418-INT (jede Taste), HW-Pull-up R36 -> +3.3V | Hardware ok, **physischer Test offen** |
| GPIO2 | LIS3DH INT1 (Bewegung), Push-Pull aktiv-LOW, nur wenn motion_mode != 0 | Hardware ok, **physischer Test offen** |
| GPIO1 | TP4056 ~CHRG (Ladekabel angesteckt), interner Pull-up | **JA - mehrfach getestet, weckt in ~30 s-2 min** |
| Timer | 6 h (nur NTP + Akkustand melden, dann sofort weiter schlafen) | JA (jeder Messpunkt) |

### Gemessene Deep-Sleep-Rate (MAX17048 CRATE + SOC-Drift, 08.09. 18:58Z-12:49Z)
| 6-h-Zyklus | SOC | Rate | Anmerkung |
|-----------|-----|------|-----------|
| 1 | 93,23 -> 89,75 % | -0,59 %/h | **Fuel-Gauge-Nachlauf** (Oberflaechenladung baut ab) |
| 2 | 89,75 -> 89,12 % | -0,11 %/h | fast eingeschwungen |
| **3** | 89,12 -> 88,80 % | **-0,05 %/h ~= 1 mA** | **eingeschwungen = echte Zahl** |

**Der MAX17048 misst keinen Strom, nur Spannung.** Direkt nach dem Abstecken liest
er die noch erhoehte Ladespannung als hohen "Verbrauch" - das ist Spannungsrelaxation,
kein Energieverlust. Erst nach ~2-4 h Ruhe stimmt die %-Anzeige. Analog beim
Anstecken: SOC springt sofort hoch (88,8 -> 96 %), obwohl nichts geladen wurde.

### Laufzeit (2000 mAh)
- **Reiner Standby (Deep-Sleep, ~1 mA):** ~1-2 Monate
- Normale Fernsehnutzung (15-25 Griffe/Tag, je ~2-3 min wach): ~2-3 Wochen
- Viel/lange Sessions: ~1 Woche
- Ohne Tiefschlaf-Schalter (nur Config D): ~24-27 h

### Warum keine separate Rail-Diagnose
Die Messung beweist es schon: waere +VSW (Display ~5 mA + Touch ~2 mA + Supervisor)
im Schlaf noch an, waere Zyklus 3 bei ~10-15 mA statt ~1 mA. 1 mA = Rail ist aus.
Zusatzbeweis: das Panel braucht nach JEDEM Wake eine volle Neu-Init (setup() faehrt
GPIO38 off->on + 300 ms settle) -> es war stromlos.

### Firmware-Stand (v23)
- v21: `assistant == 0`-Guard auf Stufe-2/WLAN-aus entfernt (WLAN darf auch bei
  HA-Sprachassistent im Ruhezustand aus; `voice_start` weckt zuerst + wartet 8 s
  auf die API).
- v22: Akku-Anzeige `85%` statt `+85%`; muted-grau, nur beim Laden gruen; neues
  `lbl_batt_chg` = ein mdi:flash-Glyph (`fonts/mdi-subset.ttf`, Font `ico_charge`),
  sichtbar nur beim Laden.
- v23: Diag-Scaffolding raus (`diag_rail_test`, `diag_rail_sim_*`,
  `diag_panelsleep_*`, `diag_out`, die 6 Diag-Buttons). `enter_sleep`/`exit_sleep`
  im ili9341_i80-Component bleiben als toter Code (harmlos).
- `sensor.remote_wz_wohnzimmer_akku_rate` seit 08.09. im HA-Recorder (Core-Neustart).

### v24-v27 (09.09.) - Wake-/Ladelogik: 3 Bugs, dann stabil

Nachdem der Kern-Deep-Sleep (v20, ~1 mA gemessen) sauber lief, wurde die Ladekabel-/
Wake-Logik ausgebaut - und dabei nacheinander gebrochen und gefixt:

1. **v24 i2c "Recovery failed" beim Deep-Sleep-Wake:** der I2C-Bus-Scan
   (setup_priority BUS=1000) lief BEVOR das Display-Component +VSW (mit den
   Pull-ups R11/R12) wieder einschaltet -> "SCL held low / Found no devices".
   Geraete initialisieren zwar danach, sieht aber nach Absturz aus.
   **Fix v24:** `on_boot` `priority: 1100` (vor dem Bus) loest die Holds + zieht
   GPIO38 LOW + `delay(180)` -> Rail steht vor dem Scan. Log jetzt:
   "Recovery: bus successfully recovered".

2. **v24 Deep-Sleep KOMPLETT blockiert:** die Bedingung hatte `!id(charger_present)`
   - das ist ein `BinarySensor*`-POINTER, immer truthy -> `!ptr` immer false ->
   Deep-Sleep-`if` nie erfuellt. **Fix v25: `!id(charger_present).state`** (4 Stellen).
   Gefunden mit einem temporaeren `interval: 30s`-Diagnose-Lambda, das jede
   Bedingung ins Log schrieb.

3. **v25 haengt in Config D (~90 mA, -4 %/h) nach Wartungsfenster / Ladekabel-
   Abziehen / Sprachabfrage:** `backlight_idle_timeout` ist der EINZIGE Weg in den
   Deep-Sleep, wird aber nur bei bestimmten Ereignissen angestossen. Laeuft es
   einmal durch und die Deep-Sleep-Bedingung ist false, endet es und probiert nie
   wieder. **Fix v26: `interval: 4min` Re-Arm** - stoesst `backlight_idle_timeout`
   neu an, wenn es NICHT laeuft (`script.is_running`-Guard) und die Idle-
   Bedingungen passen. **v26b verifiziert:** Geraet 07:18 in den Schlaf, 54 min
   Stille durch das 08:04-Wartungsfenster-Fenster (Config D haette gemeldet) ->
   echter Deep-Sleep, haelt.

4. **v26 Ladekabel-Randfall:** voller Akku -> TP4056 beendet Laden -> ~CHRG Hi-Z ->
   `charger_present` off trotz Kabel -> schlaeft nach dem Volladen wieder ein.
   **Fix:** steigende Flanke von `charger_present` startet zusaetzlich ein
   `wartungsfenster` (3 min WLAN + kein Deep-Sleep via `wartung_bis`) -> "aufs
   Kabel legen" gibt immer >=3 min OTA-Fenster.

**v27 (final, 09.09. 08:18 geflasht):** Diagnose raus, Re-Arm drin. Verifiziert:
- Deep-Sleep greift (08:32:33 in den Schlaf) und haelt
- Ladekabel-Wake: wach in ~15-20 s, 1 Transition, KEIN Loop
- i2c "bus successfully recovered" bei jedem Wake

### Bekannte Rest-Einschraenkung (klein)
Direkt nach dem Ladekabel-Anstecken kann das Geraet nach ~3-4 min nochmal kurz
einschlafen: waehrend sich die Fuel-Gauge-Spannung nach dem Anstecken einpendelt,
liest CRATE kurz "keine Ladung" UND GPIO1/~CHRG kann kurz HIGH glitchen -> beide
Ladeindikatoren sagen kurz "laedt nicht". Selbstkorrigierend (naechster ext1-Wake
per GPIO1, sobald der Ladestrom stabil ist), und die 3-min-`wartungsfenster`-Frist
reicht fuer ein OTA. Keine weitere Iteration - Nutzen/Aufwand passt nicht.
Falls es doch stoert: `delayed_off` von `charger_present` (jetzt 2 s) erhoehen,
oder Deep-Sleep-Bedingung zusaetzlich auf `akku_rate <= 0` pruefen.

### Noch offen
- **GPIO8 (Taste) + GPIO2 (Bewegung) Wecken physisch bestaetigen (Remote 2 vor Ort).**
  Einmal eine Taste druecken / die Fernbedienung anheben, waehrend sie schlaeft.
  Hardware laut Schaltplan korrekt (R36-Pull-up auf GPIO8, LIS3DH treibt GPIO2).

## Messung 2026-09-29 (Remote Remote 2, Display aus, WLAN an, BLE-Partner fehlt, ohne Kabel)

Rate = CRATE des MAX17048 (Mittel ueber die Messdauer, minuetlich aus HA gelesen), 2000 mAh.

| Stand | Rate | ~Strom | ~Laufzeit |
|---|---|---|---|
| Firmware 11:55 (mit HA-Link-Dauerwerben), kein Leichtschlaf | -14,9 %/h | ~270 mA | ~7 h |
| Firmware 13:37, Leichtschlaf aus | -8,9 %/h | ~150 mA | ~12 h |
| Firmware 13:37, Leichtschlaf an | -5,0 %/h | ~80 mA | ~20 h |
| Firmware 15:52, Leichtschlaf an + Werbung nach 60 s langsam | **-1,9 %/h** | **~35 mA** | **~2 Tage** |

Was geholfen hat:
- **Leichtschlaf** (esp_pm light_sleep_enable bei Drosselung, Tickless Idle, BT-Modem-Sleep mit Hauptquarz,
  Schleifenrunde 50 ms): halbiert etwa den Ruhestrom, WLAN und BLE bleiben verbunden.
- **Werbung ohne Partner**: vorher dauerhaft 20-40 ms (ungerichtet), jetzt nach 60 s nur noch ~1 s; jede
  Bedienung schaltet sofort zurueck auf schnell. Das war der groesste einzelne Posten, solange der Partner
  (hier: Sony aus) fehlte.
- HA-Link entfernt (warb auch verbunden weiter).

## Messung 2026-10-04 (remote-wz-wohnzimmer, Firmware mit Menue-Engine, Speicher- und Tiefschlaf-Fix)
Alltags-Setup: Leichtschlaf AN, CPU drosseln AN, BLE langsam AN, WLAN-Sparmodus AUS, WLAN aus im Ruhezustand AUS,
Tiefschlaf im Ruhezustand AN, Bluetooth nicht verbunden, Ladekabel ab. Protokoll alle 2 min aus HA.

| Phase | Zeitraum | Ladestand | Rate | ≈ Strom (2000 mAh) |
|---|---|---|---|---|
| wach, Display aus, WLAN an | 02:29-02:59 | 66,4 -> 63,7 % | **-5,5 %/h** | ~110 mA |
| Tiefschlaf | 02:59-08:25 (5 h 26 min) | 63,7 % -> 64,7 % (erste Messung, Ladekabel schon dran) | nicht messbar klein | wenige mA |

- Tiefschlaf: 5 h 26 min durchgeschlafen, kein ungewolltes Wecken (vor dem Fix: Weckgrund TIMER nach ~3 s,
  Ursache esp_pm-Leichtschlaf-Timer). Geweckt per Dockingstation-Steckdose (Weckgrund Ladekabel).
- Der Endstand ist durch das Ladekabel verfaelscht (Spannung springt beim Laden, MAX17048 rechnet aus der Spannung):
  der Verlust im Tiefschlaf liegt unterhalb der Messgenauigkeit (< ~1 % in 5,4 h).
- Vergleich September: "C" (CPU 80 MHz + BLE-Idle) -7,5 %/h; jetzt mit Leichtschlaf -5,5 %/h im Wachzustand.
- Nachtrag 09:24 (geweckt per Taste, ohne Kabel): 59,8 %. 02:59 -> 09:24 = -3,9 %, davon ~1 h wach nach dem Dock-Wecken
  (~-5,5 %) -> Tiefschlaf-Verlust liegt im Rauschen der Fuel-Gauge, hoechstens ~0,3 %/h. Hauptverbraucher ist die
  Wachzeit vor dem Einschlafen ("Tiefschlaf nach ..." kuerzer = groesster Hebel).
