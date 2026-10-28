# Wie die Referenz-Firmware Batterie spart

Quelle: https://github.com/LORDSn1per/OpenRemote-Firmware — `remote/OpenRemote_1.0.ino`
(Arduino/PlatformIO, ESP32-S3, Rev5-Board). Unsere ESPHome-Version ist die auf
Remote 2 zugeschnittene Kopie. Analysiert 2026-09-06.

## Board-Pins (identisch zu unserem)
- `PIN_TCA_INT = 8` Keypad-Interrupt, aktiv LOW
- `PIN_ACC_INT = 2` LIS3DH INT1, Polaritaet je Sleep-Stufe
- `PIN_CHARGE_STATUS = 1` TP4056 CRG_STAT, aktiv LOW
- Deep-Sleep-Weckmaske: `(1<<2) | (1<<8)`, `ESP_EXT1_WAKEUP_ANY_LOW`

## Drei-Stufen-Schlafmodell (nach Display-Timeout)

### Stufe 1: Display-Sleep (`enterDisplaySleep`)
Backlight aus, Touch-Controller schlafen, LVGL versteckt, ESP-NOW/Dock-Link
freigegeben. "Bildschirm dunkel = niemand drueckt = kein Grund, ein Radio
wachzuhalten."

### Stufe 2a: BLE-connected-idle (`enterBleConnectedIdle`)
Wenn eine BLE-HID-Aktivitaet gewaehlt ist (Chromecast/TV) — **HID-Link bleibt
aktiv**, aber:
- **Entspanntes Connection-Profil**: Intervall 120-150 ms, **Slave-Latency 3**
  (Peripheral darf bis zu 3 Connection-Events auslassen -> Funk wacht ~alle
  600 ms statt alle 15 ms), Supervision-Timeout 6 s.
  Wird **2,5 s nach** dem Idle-Eintritt angefordert (Renegotiation im
  Schlaf-Uebergangsmoment blockiert Bluedroids GAP-Task).
- **CPU auf 80 MHz** geklemmt.
- **WLAN-Stack komplett gestoppt** (`stopNetworkStack`).
- BT-Controller macht eigenen Modem-Sleep zwischen den Connection-Events.
- **Ergebnis laut Kommentar: ~15-20 mA.** Kein `esp_light_sleep_start()` hier,
  weil tickless-idle im gepinnten Arduino-Framework nicht einkompiliert ist —
  also "80 MHz + BT-Modem-Sleep + WLAN aus", CPU dreht die loop() weiter.
- Weckt auf Keypad (TCA-INT) oder Bewegung (poll alle 100 ms aus loop()).

### Stufe 2b: Light-Sleep (`enterLowPowerWait`)
Wenn KEIN BLE-Link noetig (reine IR-Fernbedienung, oder "Bluetooth Sleep" hat
den Link freigegeben):
- BT-Radio stoppen, Netzwerk-Stack stoppen.
- GPIO-Wake: `PIN_ACC_INT` HIGH-Level (Bewegung), `PIN_TCA_INT` LOW-Level
  (Keypad), + UART-Wake (USB Studio), + Timer-Wake (fuer den verzoegerten
  Deep-Sleep-Uebergang).
- `esp_light_sleep_start()` — echter Light-Sleep, CPU haelt an.
- LIS3DH-Startup-Event-Filter (Chip sendet Spuk-0x55 nach Config).

### Stufe 3: Deep-Sleep (`enterDeepPowerSleep`)
Nach `deepSleepMinutes` Display-aus-Leerlauf (der Light-Sleep-Timer-Wake loest
es aus):
- Alle geschalteten Rails/GPIOs parken, `gpio_hold_en` auf alle Power-Pins +
  die zwei INT-Pins, `gpio_deep_sleep_hold_en()`.
- `esp_sleep_enable_ext1_wakeup(ACC_INT | TCA_INT, ESP_EXT1_WAKEUP_ANY_LOW)`.
- Optionaler Timer-Wake um 03:00 fuer NTP-Resync.
- `esp_deep_sleep_start()`. ~10-100 uA.

## Connection-Profile (Konstanten)
| Profil  | Intervall (×1,25 ms) | Latency | Timeout |
|---------|----------------------|---------|---------|
| aktiv   | 12-24 (15-30 ms)     | 0       | -       |
| **idle**| **96-120 (120-150 ms)** | **3** | 600 (6 s) |
| voice   | 6-12 (7,5-15 ms)     | 0       | -       |

Renegotiation IMMER ueber `scheduleBluetoothConnectionProfile()` mit Delay
(idle: 2500 ms, aktiv: 250 ms) — nie direkt im Sleep-Uebergang.

## WLAN
`esp_wifi_set_ps(WIFI_PS_NONE)` waehrend aktiv (fuer ESP-NOW-Zuverlaessigkeit).
Beim Display-Sleep: Netzwerk-Stack **ganz aus**.

## Wake-Modi
- `WAKE_MODE_BUTTON`: Bewegungs-Wake haelt nur die MCU kurz wach fuer einen
  Tastendruck, lightet NICHT das Display. `awaitingButtonWake` + 8-s-Fenster.
- `WAKE_MODE_MOTION`: Bewegung lightet das Display.

---

## Was das fuer UNSERE ESPHome-Version heisst

Wir haben aktuell nur **Deep-Sleep (alles-oder-nichts) + wifi.disable**. Es
fehlt die **mittlere Stufe** — und die ist der Sweet-Spot fuer eine
TV-Fernbedienung: ~15-20 mA, sofort reagierend, KEINE Reconnect-Strafe.

**Plan (Prioritaet):**
1. **BLE-Idle-Connection-Profil** (groesster Hebel). Braucht eine Methode in
   `components/espidf_ble_keyboard/` die `esp_ble_gap_update_conn_params`
   aufruft: min 96 / max 120 / latency 3 / timeout 600, ~2,5 s nachdem das
   Display ausgeht; zurueck auf min 6 / max 12 / latency 0 beim Wecken oder
   bei Sprachabfrage. (Der Component hat schon `request_host_friendly_conn_params`
   als statische Funktion mit 7,5-15 ms / latency 0 — genau das Gegenstueck.)
2. **CPU 80 MHz** wenn Display aus + keine Sprachabfrage (statt 160). Unser
   `ps_cpu` macht aktuell 160.
3. **WLAN aus** wenn Display aus — haben wir schon (`ps_wifi_off` / Stufe 2).
4. Zusammen: ~15-25 mA ohne Reconnect. Deep-Sleep bleibt die "tagelang weg"-Option.
5. Optional spaeter: echten `esp_light_sleep_start()` fuer reine BLE-Nutzung
   (kein WLAN) — braucht `CONFIG_FREERTOS_USE_TICKLESS_IDLE` im sdkconfig,
   Risiko fuer den ATVV-Audiopfad, nur ausserhalb einer Aufnahme.

**Messreihenfolge zum Vergleich:** Verbrauch in (a) Display an, (b) Display aus
+ WLAN an, (c) Display aus + WLAN aus, (d) Display aus + WLAN aus + BLE-Idle-
Profil + 80 MHz, (e) Deep-Sleep. Ueber `sensor.remote_wz_wohnzimmer_akkustand`-Historie
oder USB-Messgeraet.
