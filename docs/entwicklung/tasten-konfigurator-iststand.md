# Tasten-Konfigurator: Ist-Stand der Firmware als Grundlage für das Design

Stand 2026-10-03. Ich habe nur gelesen und keine Datei geändert. Alle Zeilennummern beziehen sich auf den aktuellen Stand der Dateien. Wo die fünf Berichte sich widersprachen, habe ich im Code nachgesehen (Abschnitt 0).

**Kürzel für die Quellen:**
- **Y** = `/config/esphome/remote-wz-couch.yaml` (12 833 Zeilen). Die Remote 2-Datei wird daraus erzeugt (`/config/esphome/tools/sync_wohnzimmer_from_couch.py:20-21`).
- **TCA** = `/config/esphome/components/tca8418/tca8418.cpp` (dazu `.h` und `__init__.py`)
- **KB** / **KBH** / **KBP** = `/config/esphome/components/espidf_ble_keyboard/espidf_ble_keyboard.cpp` / `.h` / `__init__.py`
- **RC** / **RCH** / **ENC** = `/config/esphome/components/runtime_config/runtime_config.cpp` / `runtime_config.h` / `ir_encoders.h`
- **MU** / **MUH** = `/config/esphome/components/menu_ui/menu_ui.cpp` / `.h`
- **API** / **HTTP** / **JSONU** = `/root/remote-venv/lib/python3.14/site-packages/esphome/components/{api,http_request,json}/` (ESPHome 2026.9.0, `esphome/const.py:7`)
- **BUILD** = `/tmp/claude-0/-root/dfdf4e59-a397-4f35-8756-fd4fbc51989b/scratchpad/gen/.esphome/build/remote-wz-couch/`. Das ist der Build vom 2026-10-01; der aktuelle Quelltext kann leicht abweichen.
- **INO** = `.../scratchpad/fw/remote/OpenRemote_1.0.ino`
- **DOC** = `/config/esphome/esphomeremote-docs/esphomeremote-projektstatus.md`

---

## 0. Widersprüche zwischen den Berichten (im Code geprüft)

| # | Strittige Aussage | Was im Code steht |
|---|---|---|
| 1 | Zeiten der Tasten: Y:64-66 (Bericht activities) | **Y:52-54**: `key_debounce_ms "0"`, `key_long_ms "400"`, `key_dbl_ms "160"`. Es sind Werte zur Übersetzungszeit; sie stecken in `delay: ${key_long_ms}ms` (`gesture_long` Y:4975ff) und in `${key_dbl_ms}` (Y:6133, `gesture_single` Y:5051-5056). |
| 2 | `gesture_single` liegt bei Y:5093-5099 | Es liegt bei **Y:5051-5056**. |
| 3 | `menu_close_s` liegt bei Y:1197 | Es liegt bei **Y:1194** (Vorgabe 20). Das automatische `go_home` steht in `backlight_idle_timeout` bei **Y:1969-1992** (Skript beginnt bei Y:1907). |
| 4 | Globale Variable `activity` bei Y:1012 | Sie liegt bei **Y:1013** (restore, Vorgabe 0). Der Kommentar „0 = TV“ bei Y:1011-1012 ist veraltet. |
| 5 | „Im Dunkeln werden Navigationstasten **und 31** verschluckt“ (Bericht actions) | Falsch für 31. 31 ist immer eine Gestentaste (`ist_geste` Y:6115-6118) und erreicht den Zweig bei Y:6189-6196 nie. `case 31` bei Y:6191 wird also nie ausgeführt. |
| 6 | „Im Menü gehen die übrigen Tasten als Consumer-**Tap** raus“ (Bericht actions, Y:4686-4729) | Falsch. Tasten, die nicht navigieren, nutzen im Menü die Belegung der Startseite mit echtem **Halten** (`if (p == 0 \|\| !nav_taste)` Y:4602, `hold_action` Y:4638-4641). Der Tap-Block bei Y:4712-4728 wird nie erreicht. Nur Seite 99 sendet Taps (Y:4571-4585). |
| 7 | Grenze der API-Frames „etwa 64 KiB“ (Bericht ha) | Auf dem ESP32 gilt **32 768 B** (API/api_frame_helper.h:31). Ein größerer Frame lässt die Prüfung bei API/api_frame_helper_noise.cpp:194-196 scheitern; die Verbindung bricht ab. |
| 8 | Konfiguration auf der SD-Karte ablegen (Bericht ha) | Verworfen. Die SD-Karte teilt sich die Pins mit dem I2S-Mikrofon (Y:345-350) und wird nie automatisch eingebunden (`mount_on_boot: false`, Y:365). Die `rc`-Registry ist nach jedem Neustart leer (`load_on_boot: false`, Y:417). Stattdessen **NVS**, siehe 5.1. |
| 9 | Unerreichbare Fälle in `key_repeat` bei Y:4224-4225 | Tippfehler. Richtig sind **Y:6223-6225** (33/43/3/2/4/11/5). Der Grund ist der Guard bei Y:6215. |
| 10 | HA-Link (BLE) ist vorhanden | Er wurde entfernt (README.md:327-333, `archive/halink_versuch_2026-09-29/`). Die Memory-Notiz „HA-Link Stand“ ist veraltet. |
| 11 | ≈270 Aktionsplätze | Es gibt 24 Tasten. 24 × 3 Gesten × 3 Aktivitäten = **216** Plätze. |
| 12 | `popup_seite` bei Y:1293-1294 | Die globale Variable liegt bei **Y:1300**. |
| 13 | Log „keine Belegung“ bei Y:4623 oder Y:4625 | Richtig ist **Y:4627**. Der `default` steht bei Y:4623. |

---

## 1. Die physischen Tasten (vollständig, 24 Tasten am TCA8418)

**Wie die Tasten erkannt werden:**
- Treiber: der TCA8418 an Interrupt GPIO8 (Y:6064-6065), Matrix 5×5 (Y:6063-6064), `extra_gpi_pins: [5]` (Y:6066).
- Keycode in der Matrix = `row*10 + col + 1` (TCA:212-218).
- Ein GPI-Pin liefert 97 + Pin. Deshalb meldet sich die Power-Taste als **102** (`tca8418/__init__.py:37-40`).
- Die Keycodes 1 und 12 gibt es nicht. S21 ist nicht bestückt, S26 ist der Reset-Taster des ESP (DOC:63-65, 100).

**Ereignisse aus dem Treiber:**
- `on_key(keycode,row,col,pressed,long_press)` feuert beim Drücken **und** beim Loslassen (TCA:224).
- `long_press` wird erst beim Loslassen berechnet: Dauer ≥ 400 ms (TCA:201-210).
- Entprellen ist aus (`debounce: 0`, Y:6067). Begründung in Y:40-44.

**Anordnung von vorn, Display oben.** Sie stammt aus der Auswertung der Rev6-Leiterplatte im Bericht keys; die Koordinaten habe ich nicht erneut nachgemessen.
```
                       [Power 102 / S17 – oben rechts über dem Display]
 A:  Stop 5   | Rewind 4 | Play 2  | Forward 11
 B:  "Menu" 3 (links)    | Hoch 14 (Mitte)  | "Info" 31 (rechts)  <- 31 = Menütaste der Remote
 C:  Links 15            | OK 34            | Rechts 32
 D:  Zurück 13 (links)   | Runter 35        | "Return" 41 (rechts) <- "Taste rund ums D-Pad"
 E:  Vol+ 33             | Mute 44          | CH+ 42
 F:  Vol- 43             | Mic/Record 45    | CH- 22
 G:  Rot 23 | Grün 25 | Gelb 24 | Blau 21
```

**Was in der Tabelle steht:**
- Spalte „Pfad“: **G** heißt, die Taste läuft immer durch den Gestenzweig (`ist_geste` Y:6115-6118). **D** heißt `dispatch_key` (Y:4549ff).
- Die Referenznamen stammen aus INO:11045-11054.
- Schalternummer = `(4-row)*5+col+1` (INO:11516).

| KC | Schalter | Referenzname | Rolle heute | Pfad | Code |
|---|---|---|---|---|---|
| 2 | S22 | Play | Play/Pause 0x00CD | D | Y:4618 (Seite 99: Y:4576) |
| 3 | S23 | Menu | AC Home 0x0223 an den Host („Menue (S23) -> Home“); auf Seite 99 `go_home` | D | Y:4614, Y:4568 |
| 4 | S24 | Rewind | 0x00B4 | D | Y:4620 |
| 5 | S25 | Stop | 0x00B7 | D | Y:4619 |
| 11 | S16 | Forward | 0x00B3 | D | Y:4621 |
| 13 | S18 | Back | AC Back 0x0224. Im Menü `nav_back`. In Tablet + Startseite eine Geste. | D, G nur bei `seite==0 && activity==2` | Y:4613, Y:4704, Y:6117, Y:4906-4915 |
| 14 | S19 | D-Pad hoch | 0x0042 / `focus_up` | D | Y:4608, Y:4705 |
| 15 | S20 | D-Pad links | 0x0044 / `focus_left` / Regler −1 | D | Y:4610, Y:4707, Y:4681-4701 |
| 21 | S11 | Blau | **frei** | D | Y:4623, Y:4627 |
| 22 | S12 | Kanal − | nur lang: 0x009D + `release` | G | Y:4950-4966 |
| 23 | S13 | Rot | Hängelampe / Szene / Licht aus | G (mit Doppel) | Y:4943-4949 |
| 24 | S14 | Gelb | **frei** | D | Y:4623 |
| 25 | S15 | Grün | **frei** | D | Y:4623 |
| 31 | S6 | Info | **Menütaste der Remote** (Menü, Home, Display aus) | G (mit Doppel) | Y:4887-4905 |
| 32 | S7 | D-Pad rechts | 0x0045 / `focus_right` / Regler +1 | D | Y:4611, Y:4708 |
| 33 | S8 | Vol+ | 0x00E9 | D | Y:4615 |
| 34 | S9 | OK | 0x0041 (Menu Pick) / `activate` | D | Y:4612, Y:4709 |
| 35 | S10 | D-Pad runter | 0x0043 / `focus_down` | D | Y:4609, Y:4706 |
| 41 | S1 | Return | „rund ums D-Pad“: SmartTube / BUTTON_2 / App beenden | G (ohne Doppel) | Y:4933-4942 |
| 42 | S2 | Kanal + | nur lang: 0x009C + `release` | G | Y:4950-4966 |
| 43 | S3 | Vol− | 0x00EA | D | Y:4616 |
| 44 | S4 | Mute | 0x00E2 | D | Y:4617 |
| 45 | S5 | Record | Mikrofon: Suche / PTT / Assistent wechseln | G (mit Doppel) | Y:4925-4932, Loslassen Y:6151-6154 |
| 102 | S17 | Power (GPI) | „Aus-Taste“: Bild aus / Power / Reinigung | G (mit Doppel) | Y:4916-4924 |

**Was sonst noch an den Tasten hängt:**
- **102 beim Einschalten:** Im Zustand „ausgeschaltet“ schaltet nur 102 die Remote ein. Der Boot liest dafür den TCA-FIFO selbst und sucht das Ereignis `0x80|102` (Y:165-187).
- **Aufwachen aus dem Tiefschlaf über GPIO8 (ext1):** `configure_()` leert den FIFO beim Setup (TCA:145-151). Der Tastendruck, der aufweckt, geht also verloren.
- **Weitere Abnehmer:** `on_key` ist der einzige YAML-Abnehmer. `runtime_config` kennt Geräte, Befehle und Aktivitäten, aber keine Belegung der physischen Tasten (RCH:41-76).

---

## 2. Verhalten heute: Taste × Aktivität × kurz/doppelt/lang

### 2.1 Grundprinzip
- **Aktivität:** 0 = Google Streamer, 1 = Sony TV, 2 = Tablet Wohnzimmer.
  - Aktivität N ist BLE-Host-Slot N (`apply_activity` Y:2853-2861). Es gibt `host_slots: 4` (Y:5974); Slot 3 hat keine Aktivität.
  - Die Aktivität bestimmt **nur**, welcher Host die HID-Codes bekommt. Die Belegung ist **eine** für alle: `switch (id(activity)) { case 0: default: ... }` (Y:4604-4606).
  - Es gibt genau zwei Ausnahmen: 41 kurz in TV (Y:4940) und 13 als Geste in Tablet auf der Startseite (Y:6117).
- **Tasten im Pfad D (nicht Geste):**
  - Beim Drücken: `kb.hold_action("consumer_hold:0x....")` (Y:4628-4642). Beim Loslassen: `kb.execute_action("release")` (Y:6173-6176).
  - Der Host sieht die Taste gehalten und wiederholt selbst. Kurz, doppelt und lang werden dabei nicht unterschieden.
- **Tasten im Pfad G:**
  - Es gibt nur Taps. LANG feuert bei 400 ms, während die Taste noch gehalten wird.
  - DOPPELT erkennt die Remote nur bei 45, 102, 31 und 23 (`mit_doppel` Y:6120). Bei diesen Tasten wartet KURZ 160 ms nach dem Loslassen.

### 2.2 Startseite (`cur_page == 0`), alle Ziele = der aktive Slot

**Legende:** „hold“ heißt gehalten bis zum Loslassen, der Host wiederholt. „tap“ heißt etwa 50 ms. „—“ heißt, es gibt keine eigene Aktion. **Fett** markiert, was von der Aktivität abhängt.

| KC | Name | Kurz | Doppelt | Lang | A0 Streamer | A1 TV | A2 Tablet | Quelle |
|---|---|---|---|---|---|---|---|---|
| 14 | Hoch | hold 0x0042 | — (2× kurz) | Autorepeat des Hosts | gleich | gleich | gleich | Y:4608 |
| 35 | Runter | hold 0x0043 | — | Host | gleich | gleich | gleich | Y:4609 |
| 15 | Links | hold 0x0044 | — | Host | gleich | gleich | gleich | Y:4610 |
| 32 | Rechts | hold 0x0045 | — | Host | gleich | gleich | gleich | Y:4611 |
| 34 | OK | hold 0x0041 | — | Host | gleich | gleich | gleich | Y:4612 |
| 13 | Zurück | hold 0x0224 | — | Host | hold | hold | **Geste: kurz = tap 0x0224 (ohne Wiederholung), lang = `svc_tablet_close_page` → `script.turn_on script.1706872315283`, doppelt —** | Y:4613, Y:4906-4915, Y:5213 |
| 3 | „Menu“ S23 | hold 0x0223 (AC Home) | — | Host | gleich | gleich | gleich | Y:4614 |
| 33 | Vol+ | hold 0x00E9 | — | Host | gleich | gleich | gleich | Y:4615 |
| 43 | Vol− | hold 0x00EA | — | Host | gleich | gleich | gleich | Y:4616 |
| 44 | Mute | hold 0x00E2 | — | Host | gleich | gleich | gleich | Y:4617 |
| 2 | Play | hold 0x00CD | — | Host | gleich | gleich | gleich | Y:4618 |
| 5 | Stop | hold 0x00B7 | — | Host | gleich | gleich | gleich | Y:4619 |
| 4 | Rewind | hold 0x00B4 | — | Host | gleich | gleich | gleich | Y:4620 |
| 11 | Forward | hold 0x00B3 | — | Host | gleich | gleich | gleich | Y:4621 |
| 21/24/25 | Blau/Gelb/Grün | nichts (Log) | — | — | gleich | gleich | gleich | Y:4623, Y:4627 |
| 22 | Kanal − | nichts | — | tap 0x009D, danach `release` | gleich | gleich | gleich | Y:4950-4966 |
| 42 | Kanal + | nichts | — | tap 0x009C, danach `release` | gleich | gleich | gleich | Y:4950-4966 |
| 23 | Rot | `svc_haengelampe_toggle`, wartet 160 ms | `svc_szene_wz_tv` (`scene.turn_on scene.wohnzimmer_tv`) | `svc_wz_licht_aus` (`light.turn_off light.wohnzimmer`) | gleich | gleich | gleich | Y:4943-4949, Y:5231-5242 |
| 41 | Return | **A0/A2: `svc_app_smarttube`** (`androidtv.adb_command` an `${ent_adb}`, startet SmartTube **immer auf dem Streamer**); **A1: `taste:2`** (HID-Button 2 = Sony-Schnelleinstellungen) | — | `svc_streamer_close_app` (`script.esphomeremote_streamer_close_app`) | SmartTube | BUTTON_2 | SmartTube (Streamer) | Y:4933-4942, Y:5446, Y:5454 |
| 31 | Menütaste | `open_menu` (letzte Menüseite, wenn innerhalb `mem_s`), wartet 160 ms | Display sofort aus (`sofort_aus` + `backlight_idle_timeout`); war es beim ersten Druck dunkel, nur wecken | `open_menu_top` | gleich | gleich | gleich | Y:4887-4905, Y:4816, Y:4803 |
| 102 | Power | `svc_tv_screen_off` (`script.tv_wohnzimmer_bildschirm_aus`), wartet 160 ms | `clean_run` (Reinigungsmodus) | tap `consumer:0x0030` an den aktiven Host | gleich | gleich | gleich | Y:4916-4924, Y:5304, Y:2166 |
| 45 | Mikrofon | tap 0x0221 (AC Search), wartet 160 ms | `toggle_assistant` (Google ↔ HA) | `voice_start` (Push-to-Talk); Loslassen setzt `voice_want=false` und startet `voice_stop` | gleich | gleich | gleich | Y:4925-4932, Y:6151-6154, Y:3913, Y:4090, Y:4144 |

**Taste 23 kurz im Detail:**
- Die Bedingung ist `binary_sensor haengelampe`, also `light.hangelampe` (Y:6351-6354).
- Ist die Lampe an: `light.turn_off`.
- Sonst: `light.turn_on` mit `brightness_pct 100` und `color_temp_kelvin 2000` (Y:5231-5238).

**Taste 45 lang im Detail:**
- Mit dem Assistenten Google (`assistant==0`, Y:1267) sendet die Taste je nach `google_mode` (Y:1272) und App entweder `atv_mic_start` an den aktiven Slot oder `consumer:0x0221` (SmartTube).
- Mit HA läuft `voice_assistant.start`. Dabei wartet die Remote bis zu 8 s auf `api.connected` (Y:4050-4062).
- Ist die SD-Karte eingebunden, ist Sprache gesperrt (Y:3916-3920).

### 2.3 Auf Menüseiten (`cur_page ≠ 0`)

| Taste | Remote-Menü (Seiten ≠ 0/99/41/26) | SD-Menü (Seite 99) |
|---|---|---|
| 14/35/15/32 | `focus_up/down/left/right` mit Software-Wiederholung. 15/32 verstellen stattdessen einen fokussierten Regler um ±1 (Y:4681-4709). | `menu->focus_move(dx,dy)` mit Wiederholung (Y:4559-4562) |
| 34 OK | `activate`, keine Wiederholung | `focus_activate()` |
| 13 Zurück | `nav_back` mit Wiederholung (keine Geste, auch nicht in Tablet) | `go_back()`, an der Wurzel `go_home` (Y:4564-4567) |
| 31 | kurz `go_home` (Y:4903), lang `open_menu_top`, doppelt Display aus | wie links |
| 3 | hold 0x0223 an den Host | `go_home` (Y:4568) |
| 33/43/44/2/5/4/11 | hold an den Host, wie auf der Startseite (Y:4602) | **tap** (`consumer:`) (Y:4571-4585) |
| 102/45/23/41/22/42 | dieselben Gesten wie auf der Startseite; `ist_geste` prüft die Seite nicht (Y:6116) | gleich |
| 21/24/25 | nichts | nichts |

Sonderseiten 41 (Tastatur), 26 (Sprachantwort), 24, 34, 35 und 36 stehen in Abschnitt 4.

### 2.4 Lücken im heutigen Stand
- Für alle Tasten im Pfad D (15 Stück) gibt es **kein** kurz/doppelt/lang.
- 21, 24 und 25 sind frei. 22 und 42 tun bei kurzem Druck nichts.
- Eine eigene Belegung je Aktivität fehlt; es gibt nur die beiden Ausnahmen aus 2.1.
- **Keine Taste sendet IR.** IR läuft nur über `runtime.json` und die Dienste `remote_send_command` / `remote_run_activity` (Y:720-728).
- Die HA-Buttons „Verbinden: Streamer/TV/Tablet“ (Y:6317-6340) rufen nur `switch_host` auf und setzen `activity` nicht. Beim nächsten `apply_activity`, etwa beim Boot (Y:326), schaltet die Remote auf den alten Slot zurück.
- Die BLE-Seite setzt `activity`, ohne `apply_activity` aufzurufen (Y:9827, 9859). Ebenso die Tablet-Seite (Y:9203-9208).

---

## 3. Aktions-Primitive zur Laufzeit (C++)

**Globale Objekte in eigenen Headern:**
- Die `includes:` (Y:95-103) stehen in `main.cpp` hinter allen globalen Objekten. Belege: `#include "esphomeremote-pm.h"` bei BUILD/src/main.cpp:8257; `kb` bei :696, `ir_tx` bei :83, `activity` bei :2819.
- Ein neuer Header `esphomeremote-tasten.h` kann deshalb `id(kb)`, `id(ir_tx)`, `id(rc)`, `id(menu)`, `id(activity)` und `id(<script>)->execute()` genauso benutzen wie ein Lambda.

### 3.1 BLE-HID an den aktiven Slot

**Einstellungen** (Y:5967-5990):
- `host_slots: 4`, `pointer: false`, `alpha_keyboard: ${kb_tasten}` = `"true"` (Y:62), `keyboard_layout: de`, `web_control: false`.
- `ha_action` und `api_services` sind nicht gesetzt, also aus (Vorgabe in KBP:277).

**Reports im HID-Deskriptor:**
- Tastatur: Report 1, Usages 0..0x73 (KB:53).
- Consumer: Report 2, 0..0x3FF (KB:73-75).
- System: Report 3 (KB:86).
- Buttons 1..8: Report 6 (KB:103).

Der Nur-Ziffern-Modus (`kb_ziffern`, Y:1287, gesetzt bei Y:295) schränkt die Tastatur-Usages ein.

**Grammatik von `execute_action`** (KB:3616-3938), in der Reihenfolge, in der sie geprüft wird:

| Form | Wirkung |
|---|---|
| `consumer:0xNNNN` | Consumer-Tap (KB:3730 → `send_consumer` KB:3168) |
| `combo:mod:key` | Tastatur-Tap (KB:3719 → `send_key_combo` KB:3112) |
| `taste:n` | HID-Button n, 1..8 (KB:3725 → `send_button` KB:3157) |
| `consumer_hold:0x…`, `key_hold:mod:key`, `hold:<x>` | halten bis `release` (KB:3750-3773) |
| `release` | gibt **alles** Gehaltene frei (`release_held` KB:3236) |
| `a\|b\|c` | Kette mit blockierenden 50 ms dazwischen (KB:3692-3710) |
| `delay:ms` | `vTaskDelay`, **blockiert die Hauptschleife** (KB:3712-3717) |
| `repeat:N:…` | Body N-mal hintereinander (KB:3633-3645) |
| `alternate:A \|\| B` | bei jedem Aufruf der nächste Zweig (KB:3656-3690) |
| `switch_host:n`, `forget_host:n` | Slots wechseln oder vergessen (KB:3840-3851) |
| `ha_action:dom.svc;k=v` | HA-Aufruf, **nur mit `ha_action: true`** (KB:3856-3859, 4023-4070) |
| benannte Aktionen | `remote_power` (0x30), `power` (= System Power Down 0x81!), `home` (0x223), `back` (0x224), `up/down/left/right`, `search` (0x221), `menu`, `ok` (= Enter!), `channel_up/down` (= PgUp/PgDn), `color_red..blue` (= F1-F4), `num0..9`, `spare1..16` (KB:3314-3392, 3883-3906) |
| `press_button:<id>`, `macro:<name>`, `atv_mic_start/stop` | KB:3925, 3930, 3620-3628 |
| sonst alles | wird **als Text getippt** (KB:3936-3937). Ein Tippfehler in einer Aktion landet also als Text beim Host. |

```cpp
// Tap an den aktiven Slot
id(kb).execute_action("consumer:0x00E9");      // Vol+   (== id(kb).send_consumer(0x00E9))
id(kb).execute_action("taste:2");              // HID-Button 2 (Sony-Schnelleinstellungen)
id(kb).execute_action("combo:0x00:0x28");      // Enter
id(kb).execute_action("remote_power");         // Consumer Power 0x0030 ("power" waere System Power Down)

// Echtes Halten (so arbeitet dispatch_key heute, Y:4628-4641 / Y:6175)
id(kb).hold_action("consumer_hold:0x0042");    // beim Druecken: Host wiederholt selbst
id(kb).execute_action("release");              // beim Loslassen (gibt ALLES Gehaltene frei)

// Tap nach einer gehaltenen Taste: send_consumer stellt held_consumer_ wieder her
// (KB:3168-3189) -> danach "release" senden, siehe der Fehler bei Y:4955-4965
id(kb).execute_action("consumer:0x009C"); id(kb).execute_action("release");
```

### 3.2 BLE an einen bestimmten Slot („beliebiges Gerät“)

**Grundsatz:** Jeder `send_*` nutzt `conn_id_` und verwirft das Paket stillschweigend, wenn `!is_connected_`. Eine Warteschlange gibt es nicht. Es gibt **keinen** Aufruf, der an einen anderen oder geparkten Slot sendet.

**Was `switch_host(slot)` tut** (KB:2293-2413):
1. `release_held()`
2. **NVS-Schreibvorgang** über `save_host_slots_()` (KB:2306 → `nvs_set_blob`/`nvs_set_u8` KB:1356-1387)
3. Mit `parallel_` (`ble_parallel`, Vorgabe true, Y:1288): Die aktuelle Verbindung wird geparkt. Es bleibt **höchstens eine** geparkte Verbindung; andere werden getrennt (KB:2347-2369).
4. Ist das Ziel die geparkte Verbindung, ist es **sofort** aktiv (KB:2370-2385). Sonst wirbt die Remote, und die Verbindung kommt erst nach Sekunden.

**Grenzen:**
- `apply_activity` geht davon aus, dass `activity == active_host_slot()` gilt. Ein vorübergehender Wechsel muss zurückschalten.
- Ein Wechsel für einen einzigen Tastendruck kostet zwei NVS-Schreibvorgänge und gibt gehaltene Tasten frei.

```cpp
// Behelf mit dem heutigen Fork: nur sofort, wenn s der EINE geparkte Slot ist
inline bool sende_an_slot(uint8_t s, const std::string &act) {
  const uint8_t prev = id(kb).active_host_slot();
  if (s >= id(kb).host_slots()) return false;
  if (prev != s) id(kb).switch_host(s);              // schreibt NVS!
  const bool ok = id(kb).is_connected();
  if (ok) id(kb).execute_action(act);
  else ESP_LOGW("tasten", "Slot %u nicht verbunden - '%s' verworfen", s, act.c_str());
  if (prev != s) id(kb).switch_host(prev);           // id(activity) == aktiver Slot halten
  return ok;
}
```

**Empfehlung für „beliebiges Gerät“:**
- Im Fork `send_consumer_to(slot, usage)` und `send_button_to(slot, n)` ergänzen. Diese senden `esp_ble_gatts_send_indicate` an die `conn_id` aus `standby_`, wenn der Slot geparkt ist.
- Ist er nicht verbunden: Ablehnung mit Log und kein `switch_host`.
- Im Konfigurator: Ziel „aktive Aktivität“ als Standard. „Fester Slot“ nur mit dem Hinweis „nur zuverlässig, wenn dieses Gerät gerade verbunden oder geparkt ist“.

### 3.3 IR

**Hardware:**
- Sender `remote_transmitter id: ir_tx` an GPIO5, invertiert, 50 % Tastgrad (Y:369-375). Er ist **immer** verfügbar.
- Empfänger `ir_rx` an GPIO4. Er wird über `ir_rx_power` versorgt (Y:379-401).

**Registry `rc`:**
- Sie liest `/sd/runtime.json`. Es gibt `send_command`, `send_command_by_name`, `run_activity` (RCH:113-116).
- Sie füllt sich nur nach `sd_einbinden` oder `sd_neuladen` (Y:2559-2597) und ist nach jedem Neustart leer. Für Tasten ist sie deshalb ungeeignet.

**Encoder des Projekts** (ENC:308 `ir_encode_parsed`): NEC/NECext/NEC42, SIRC 12/15/20, RC5/RC5X, RC6, Samsung32, Kaseikyo/Panasonic/Denon, Pioneer, RCA, JVC und Pronto (laut Bericht actions).

**Anlernen:** Heute über `on_raw` → `rc->learn_capture` (Y:387-401). Das schreibt auf die SD-Karte. Der Rohvektor `x` ist aber auch ohne SD-Karte verfügbar und könnte als HA-Event an den Konfigurator gehen.

```cpp
// (a) Roh-Timings aus der Konfiguration (+Mark/-Space in us)
inline void ir_roh(const int32_t *T, size_t n, uint32_t f = 38000, int wdh = 1) {
  auto call = id(ir_tx).transmit();
  auto *d = call.get_data();
  d->set_carrier_frequency(f);
  d->reserve(n);
  for (size_t i = 0; i < n; i++) { if (T[i] > 0) d->mark(T[i]); else d->space(-T[i]); }
  call.set_send_times(wdh);
  call.perform();
}
// (b) Protokoll + Adresse + Befehl (Flipper/IRDB-Konvention, wie runtime.json "parsed")
inline bool ir_proto(const std::string &proto, uint32_t addr, uint32_t cmd) {
  runtime_config::IrTimings t; uint32_t f = 38000;            // IrTimings = std::vector<int32_t> (ENC:23)
  if (!runtime_config::ir_encode_parsed(proto, addr, cmd, t, f)) return false;   // ENC:308
  ir_roh(t.data(), t.size(), f);
  return true;
}
// (c) Encoder von ESPHome direkt
id(ir_tx).transmit<remote_base::NECProtocol>({0x04, 0x08, 1});
```

### 3.4 Home Assistant (beliebiger Dienst, beliebige Entität)

**Voraussetzungen, die schon erfüllt sind:**
- `USE_API_HOMEASSISTANT_SERVICES` und `USE_API_HOMEASSISTANT_STATES` sind gesetzt (BUILD/src/esphome/core/defines.h:29-30). Grund: Die YAML nutzt `homeassistant.service` (API/__init__.py:718).
- In HA ist `allow_service_calls: true` für beide Remotes gesetzt (`/config/.storage/core.config_entries` Zeile 448 = remote-wz-wohnzimmer, 450 = remote-wz-couch).
- `kb` erbt von `api::CustomAPIDevice` (KBH:107-109). `call_homeassistant_service(svc, std::map<string,string>)` steht bei API/custom_api_device.h:268.

```cpp
// (A) geht heute ohne YAML-Änderung
id(kb).call_homeassistant_service("input_boolean.toggle", {{"entity_id", "input_boolean.kochen"}});
id(kb).call_homeassistant_service("light.turn_on",
    {{"entity_id", "light.hangelampe"}, {"brightness_pct", "100"}, {"color_temp_kelvin", "2000"}});
id(kb).call_homeassistant_service("script.turn_on", {{"entity_id", "script.tv_wohnzimmer_bildschirm_aus"}});
id(kb).fire_homeassistant_event("esphome.remote_taste", {{"taste", "23"}, {"art", "lang"}});  // Name muss mit "esphome." beginnen

// (B) Als Zeichenkette in der Aktionsgrammatik (verkettbar mit '|'):
//     braucht `ha_action: true` unter espidf_ble_keyboard (KBP:272, Gate KB:4025-4028)
id(kb).execute_action("ha_action:light.toggle;entity_id=light.sternenhimmel");

// (C) menu_ui-Trigger: nur service + entity_id (Y:446-452, MUH:137-141)
id(menu)->fire_ha_service_("cover.open_cover", "cover.wohnzimmer_rollo");
```

**Zustellung (wichtig):**
- Es gibt keine Warteschlange. Ohne verbundenen Client geht der Aufruf verloren (API/api_server.cpp:432-440; KB:4058-4061 warnt nur).
- Das WLAN kann aus sein (Abschnitt 6). `key_ohne_wecken` → `wlan_frueh_an` (Y:2456-2471) schaltet es nur wieder ein.
- Die Laufzeit braucht deshalb einen verzögerten Versand. Vorbild ist `voice_start` mit `wait_until api.connected, timeout 8s` (Y:4050-4060).

```cpp
// Warteschlange für HA-Aufrufe (aus loop() der neuen Komponente oder einem interval: pumpen)
struct HaJob { std::string svc; std::map<std::string, std::string> data; uint32_t t0; };
inline std::vector<HaJob> &ha_q() { static std::vector<HaJob> q; return q; }
inline void ha_senden(std::string svc, std::map<std::string, std::string> d) {
  if (api::global_api_server && api::global_api_server->is_connected()) { id(kb).call_homeassistant_service(svc, d); return; }
  if (ha_q().size() < 8) ha_q().push_back({std::move(svc), std::move(d), millis()});
}
inline void ha_pumpen() {
  auto &q = ha_q(); if (q.empty()) return;
  const bool con = api::global_api_server && api::global_api_server->is_connected();
  for (auto it = q.begin(); it != q.end();) {
    if (con) { id(kb).call_homeassistant_service(it->svc, it->data); it = q.erase(it); }
    else if (millis() - it->t0 > 8000) { ESP_LOGW("tasten", "HA-Aufruf %s verworfen", it->svc.c_str()); it = q.erase(it); }
    else ++it;
  }
}
```

### 3.5 Funktionen der Remote („Intern“)

| Funktion | Aufruf | Quelle |
|---|---|---|
| Menü öffnen (letzte Seite innerhalb `mem_s`) | `id(open_menu)->execute();` | Y:4816-4871 |
| Menü ganz oben (Fokus-Gedächtnis leeren) | `id(open_menu_top)->execute();` | Y:4803-4814 |
| Home / Menü schließen | `id(go_home)->execute();` | Y:4754 |
| Menütaste „kurz“ 1:1 wie heute (kontextabhängig) | siehe Snippet unten | Y:4891-4904 |
| Display sofort aus | `id(sofort_aus) = true; id(backlight_idle_timeout)->execute();` | Y:4897, Y:1907 |
| Display wecken | `id(wake_backlight)->execute();` | Y:2473 |
| Zurück (Popups, dann Menübaum) | `id(nav_back)->execute();` | Y:5106-5120 |
| Fokus/OK im Menü | `focus_up/down/left/right`, `activate` | Y:3878-3905 |
| SD-Menü | `id(menu)->focus_move(dx,dy)`, `focus_activate()`, `go_back()`, `at_root()` | Y:4559-4568 |
| Sprache Push-to-Talk | `voice_start`; **das Loslassen braucht** `id(voice_want)=false; id(voice_stop)->execute();` | Y:3913, Y:6151-6154 |
| Assistent Google ↔ HA | `id(toggle_assistant)->execute();` | Y:4144 |
| Aktivität wechseln | `id(activity) = n; id(apply_activity)->execute();` (Streamer zusätzlich `streamer_hdmi`) | Y:2853, Y:7896-7920, Y:5310 |
| Reinigungsmodus | `id(clean_run)->execute();` | Y:2166 |
| Herunterfahren („aus“) | `id(herunterfahren)->execute();` | Y:5571 |
| Mikrofon an/aus, Overlays, Tastenlicht | `toggle_mic`, `toggle_bri_overlay`, `toggle_kbd_overlay`, `toggle_kbd_light` | Y:3034, Y:3065, Y:3094, Y:5180 |
| Tastatur-Popup | `kbd_popup_zeigen` / `kbd_popup_weg` | Y:4792-4802 |
| Beliebige LVGL-Seite | `id(page_x)->get_parent()->show_page(id(page_x)->index, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 150);` (`on_load` setzt `cur_page`) | laut Bericht actions |

```cpp
// Menütaste "kurz" - exakt die heutige Logik aus gesture_action case 31 (Y:4891-4904)
inline void menue_kurz(bool dunkel_beim_ersten_druck) {
  if (id(cur_page) == 24 && (dunkel_beim_ersten_druck || millis() < 6000)) return;   // Begrüßung
  if (id(cur_page) == 41 && id(kbd_popup)) { id(kbd_popup_zu) = true; id(kbd_popup_weg)->execute(); }
  else if (id(cur_page) == 0 && id(kbd_popup_zu) && id(kbd_streamer_offen)) { id(kbd_popup_zu) = false; id(kbd_popup_zeigen)->execute(); }
  else if (id(cur_page) != 0) id(go_home)->execute();
  else id(open_menu)->execute();
}
inline void menue_doppelt(bool dunkel) { if (id(cur_page) == 24 && (dunkel || millis() < 6000)) return;
  if (!dunkel) { id(sofort_aus) = true; id(backlight_idle_timeout)->execute(); } }
inline void menue_lang(bool dunkel) { if (id(cur_page) == 24 && (dunkel || millis() < 6000)) return;
  id(open_menu_top)->execute(); }
```

### 3.6 Wo eine Laufzeit-Engine eingreift
- **Einstieg:** `tca8418: on_key` (Y:6069ff). Den Vorspann übernehmen (4.1), dann `tasten_ereignis(keycode, pressed)` aus dem Header aufrufen.
- **Ausgabe:** Die heutigen Abläufe `gesture_action` (Y:4872-4971) und `dispatch_key` (Y:4549-4728) werden ersetzt, wenn eine gültige Konfiguration geladen ist.
- **Rückfall:** Ohne gültige Konfiguration bleibt die heutige Logik aktiv.

---

## 4. Kontextregeln, die erhalten bleiben müssen

### 4.1 Vorspann in `on_key`, bei jedem Ereignis, in dieser Reihenfolge (Y:6070-6095)
1. `key_was_dark = disp_off`. Ist es dunkel, kommt ein Eintrag in `weck_log` (Y:6074-6075).
2. **Wecken:** `!disp_off || keys_wake_all || keycode == 31` → `wake_backlight`, sonst `key_ohne_wecken` (Y:6078-6081).
   - `keys_wake_all` hat die Vorgabe false (Y:1337), also „Nur Menütaste“.
   - **Fest auf 31 codiert.** Ist die Menüfunktion frei verlegbar, muss die Regel lauten: „die Taste, die eine Menüfunktion trägt“.
3. `last_input_ms = millis()` (Y:6083). Das treibt die Timer für WLAN und Tiefschlaf.
4. **Reinigungsmodus:** Alle Tasten sind gesperrt. Nur `!pressed && keycode==13 && long_press` (Treiber-Flag ≥ 400 ms) beendet ihn (Y:6087-6090). Das ist fest auf 13 codiert und muss unabhängig von der Konfiguration bleiben.
5. **Touch-Test (Seite 36):** Nur ein Druck auf 13 führt `nav_back` aus, alles andere wird ignoriert (Y:6092-6095).
6. Danach folgt der Gestenzweig (Y:6108-6169) oder `dispatch_key` (Y:6171-6232).

### 4.2 Navigation, solange eine Menüseite offen ist (`cur_page ≠ 0`)
- `nav_taste` = 14, 35, 15, 32, 34, 13 (Y:4600-4601). Diese Tasten gehören dem Display. Alle anderen Tasten verhalten sich wie auf der Startseite (Y:4602).
- **Seite 99:** eigene Navigation in `menu_ui`; 3 = `go_home`; Medientasten als Tap (Y:4557-4586).
- **Seite 41 (Tastatur, auch als Popup):** D-Pad und OK bewegen bzw. drücken in der Matrix (`eingabe::weiter`, `kbd_press` Y:5028). 13 fällt durch zu `nav_back` und schließt das Popup (Y:4649-4664, Y:5106-5117).
- **Seite 26:** 14 und 35 scrollen die Sprachantwort (Y:4669-4671).
- **Fokussierter Regler:** 15 und 32 verstellen ihn um ±1 (Y:4681-4701).
- **Vorschlag:** Die konfigurierbare Belegung gilt auf Seite 0 vollständig. Auf Menüseiten gilt sie für alle Tasten außer den sechs `nav_taste`. Diese bleiben fest, solange `cur_page ≠ 0` ist. Das muss im Konfigurator sichtbar sein, zum Beispiel als Schloss-Symbol mit „im Menü: Navigation“.

### 4.3 Sonderseiten und Zustände
- **24 (Begrüßung):** 31 tut nichts, wenn es beim ersten Druck dunkel war oder `millis()<6000` gilt (Y:4891). 13 führt `go_home` nur aus, wenn `!key_was_dark && millis()>6000` (Y:5178).
- **34 (Tasten-Test, `button_test_mode`):**
  - Alle Tasten laufen durch die Gestenerkennung und gelten dabei als doppelfähig (Y:6122). Sie werden **nur angezeigt** (Y:4879-4885).
  - Der Modus wird nur über den Touch-Button „<“ zurückgesetzt (Y:12788). Die Seite hat kein `on_unload`; der Modus kann also hängen bleiben.
- **35 (Reinigung):** Sperre wie in 4.1.4.
- **36 (Touch-Test):** wie in 4.1.5.
- **Popups** (`popup_seite`, Y:1300; Seiten 12, 40, 41): 13 schließt das Popup und geht nach Home (Y:5109-5117).
- `kbd_streamer_offen` setzt HA über `remote_show_keyboard` / `remote_hide_keyboard` (Y:738-757). Das wirkt sich auf 31 kurz aus (siehe 3.5).

### 4.4 Regeln bei dunklem Display
- Mit `keys_wake_all=false` arbeiten alle Tasten außer 31 blind. Ihre Aktion wird trotzdem ausgeführt. Dazu kommen ein schnelles BLE-Profil und WLAN früh an (`key_ohne_wecken` Y:2456-2461).
- `key_was_dark && cur_page != 0`: Ein Druck auf 14/35/15/32/34/13 wird **verschluckt** (Y:6189-6196). Weil er auch nicht weckt, tut er gar nichts. Das gilt, bis nach `menu_close_s` (Y:1194, Vorgabe 20 s) automatisch `go_home` läuft (Y:1969-1992, nicht auf Seite 24).
- Bei einem Doppeldruck zählt die Dunkelheit des **ersten** Drucks (`gest_dark`, Y:6137). Deshalb weckt 31 doppelt im Dunkeln nur und schaltet nicht aus (Y:4897).

### 4.5 Halten und Wiederholen
- **Startseite und Tasten, die nicht navigieren:** echtes Halten, der Host wiederholt (Y:4629-4642).
  - Beim Loslassen einer Taste aus Pfad D kommt ein globales `release` (Y:6175).
  - Es gibt **keine** automatische Freigabe als Sicherheitsnetz (`max_key_hold_ms` = 0, KBP:260).
- **Menü:** Software-Wiederholung `key_repeat` (Y:4732-4751, `mode: restart`).
  - Ablauf: 300 ms Pause, dann alle 55 ms `dispatch_key`, solange `held_key != 0`. Notbremse bei 170 Durchläufen, mit `release` ab 160.
  - Wiederholt werden nur 14/35/15/32/13, nicht 34 (Y:6213-6231).
- **Gestentasten** tappen nur, es gibt kein Halten. LANG feuert genau einmal; 22 und 42 wiederholen nicht.

### 4.6 Zeiten der Gesten (Übersetzungszeit, Y:52-54)
- **LANG:** Druck + 400 ms, feuert **während des Haltens** (`gesture_long` Y:4975-4986, gestartet bei Y:6139).
- **DOPPELT:** Der zweite Druck muss weniger als 160 ms nach dem ersten Loslassen kommen (Y:6132-6133). Die Aktion feuert beim zweiten Loslassen (Y:6156-6160).
- **KURZ:**
  - sofort beim Loslassen (Y:6164-6167), oder
  - bei Tasten mit Doppelaktion 160 ms später (`gesture_single` Y:5051-5056).
- **Geteilter Zustand:** Es gibt einen einzigen Satz `gest_*`-Variablen (Y:1061-1085, 1169, 1278) und je **ein** Skript `gesture_long` / `gesture_single`, beide im `restart`-Modus.
  - Zwei Gestentasten innerhalb von 160 ms beeinflussen sich gegenseitig.
  - Ein Tipp gefolgt von einem Halten ergibt nur LANG; der erste Tipp geht verloren.
- **Regeln für die konfigurierbare Engine, abgeleitet aus dem heutigen Verhalten:**
  - Doppelerkennung **nur**, wenn für die Taste eine Doppelaktion konfiguriert ist. Sonst bekäme jede Taste 160 ms Verzögerung.
  - Hat die Taste nur eine HID-Aktion für KURZ und weder LANG noch DOPPELT, bleibt es beim Halten: Druck = hold, Loslassen = release, der Host wiederholt. Im Konfigurator ist das eine Option „gedrückt halten / wiederholen“.
  - Sobald LANG oder DOPPELT belegt ist, gibt es nur Taps.
  - Push-to-Talk braucht einen Haken beim Loslassen. Das heißt: Aktionstyp „Sprache (halten)“ nur für LANG.
  - Die Zeiten 400/160 sind heute fest einkompiliert. Sollen sie einstellbar werden, braucht es `delay: !lambda` oder eine Zeitsteuerung in C++.

### 4.7 Feste Regeln, die keine Konfiguration überschreiben darf
- **Einschalten aus „ausgeschaltet“ nur mit 102.** Das passiert beim Boot, bevor eine Konfiguration geladen ist (Y:165-187).
- **Reinigungsmodus beenden:** 13 lang (Y:6087-6090). **Touch-Test verlassen:** 13 (Y:6092-6095).
- **Mindestens eine Taste muss „Menü öffnen“ tragen.** Der Konfigurator sollte sonst warnen. Diese Taste muss auch die Weck-Regel aus 4.1.2 bekommen.
- **Sonderfall für 31 als Menütaste:** Liegt die Menüfunktion auf einer `nav_taste`, etwa 13, kollidiert sie im Menü mit der Navigation. Das ist eine Designentscheidung, die zu treffen ist; mein Vorschlag ist, dass im Menü die Navigation Vorrang hat.

---

## 5. Transport, Speicherung und HA-Hosting

### 5.1 Speicherung auf der Remote: NVS-Blob, im Betrieb als Tabelle im PSRAM

**Warum NVS:**
- Die NVS-Partition ist 0x70000 = 448 KB groß. Die App-Slots haben je 0x7C0000 (BUILD/partitions.csv). Die Firmware ist 2,39 MB groß (BUILD/build/firmware.ota.bin).
- Die Partitionstabelle bleibt, also ist kein Flashen über Kabel nötig.
- Ein ersetzter Blob in NVS übersteht Stromausfall: Der alte Wert bleibt erhalten.
- Vorbild ist die direkte NVS-Nutzung in `espidf_ble_keyboard` mit eigenem Namespace (`"espidf_ble_kb"`, Überschreibungen bei KB:1390-1525). Diese Speicherung ist mit 8 × 255 B je Slot zu klein, aber das Muster passt.

**Aufbau:**
- Namespace `"rcfg"` mit drei Schlüsseln:
  - `cfg` = Blob mit JSON oder einer kompakten Form, die HA erzeugt
  - `cfg_h` = u32 FNV-1a, derselbe Algorithmus wie `fw_hash` bei Y:620-623
  - `cfg_v` = Schemaversion

**Laden:**
- Im `setup()` einer neuen Komponente `components/button_config/`. NVS ist dann schon offen; ESPHome-Preferences nutzen es, und `kb` liest NVS in seinem eigenen `setup()`.
- Schritte: `nvs_get_blob` in einen PSRAM-Puffer, Hash und Version prüfen, mit `json::SpiRamAllocator` parsen (JSONU/json_util.h:123), daraus eine **flache POD-Tabelle** `[aktivität][taste][geste] → {typ, ziel, code, str_off}` in **einer** PSRAM-Arena bauen, Strings als Offsets in einem PSRAM-Pool ablegen, dann `JsonDocument` freigeben.
- Bei jedem Fehler bleibt die heutige fest eingebaute Belegung aktiv.

**Schreiben:**
- Aus der Hauptschleife und nicht während Sprache aktiv ist (`voice_active` Y:1309, `host_mic` Y:1103).
- Grob 0,1-0,5 s Flash-Stall.
- Wie sich die Bootzeit mit dem größeren NVS ändert, mit `nvs_statistik()` messen (`esphomeremote-wlan.h:77-90`, Ausgabe beim ersten API-Connect, Y:610).

### 5.2 Transport von HA zur Remote: „HA kündigt einen Hash an, die Remote holt die Datei“ (empfohlen)

**Auf der HA-Seite:**
1. Die Integration speichert die Konfiguration und schreibt die Gerätedatei, z. B. `/config/www/esphomeremote/remote_wz_couch.json`.
2. Sie setzt eine Entität mit dem Hash, z. B. `sensor.esphomeremote_<gerät>_cfg_hash`.
3. Der Gerätename kommt aus `App.get_name()`. So läuft die erzeugte Remote 2-YAML ohne Änderung.

**Auf der Remote:**
1. Ein `text_sensor: platform: homeassistant` beobachtet diesen Hash. Vorbild ist das Muster bei Y:6807-6810. HA schickt die Zustände bei jedem API-Connect neu, das ergibt den automatischen Abgleich nach jedem Aufwachen.
2. `on_value`: Weicht der Hash von `cfg_h` ab und läuft kein `online_image`-Download, holt die Remote die Datei mit `http_request` und `capture_response: true`.
   - Bisher gibt es diese Aktion nirgends; sie fügt `USE_HTTP_REQUEST_RESPONSE` hinzu (HTTP/__init__.py:351-355).
   - `max_response_buffer_size` hat die Vorgabe 1 kB (HTTP/__init__.py:281) und muss z. B. auf 64 kB steigen. Der Puffer liegt im PSRAM.
   - Die Aktion ist synchron und blockiert die Schleife kurz.
   - Alternative: In der neuen C++-Komponente die Antwort selbst stückweise in PSRAM lesen.
3. Im Antwort-Handler: Status prüfen, Hash des Bodys gegen den angekündigten Hash prüfen (fängt halb geschriebene Dateien ab), parsen, NVS schreiben, Tabelle atomar tauschen.
4. **Sofort anwenden:** ein neuer API-Dienst `remote_config_check` neben `remote_reload_config` (Y:729-731). Muster wie `refresh_cover` (Y:684-692). Mit `supports_response` kann die Remote den gespeicherten Hash zurückgeben, und das Panel zeigt „übernommen“.
5. Optional in `on_client_connected` (Y:605-642): ein Event `esphome.remote_config_stand` mit dem Hash, nach dem Muster bei Y:636-640.
6. **Während der Übertragung** Schlaf und WLAN-Aus sperren: `wartung_bis = millis()+60000` (Y:1249). Die Guards gibt es schon: Y:1838 (WLAN), Y:1811 und 1894 (Tiefschlaf). Alternativ `ota_aktiv` (Y:1251).
7. Bekannte Falle: Zwei HTTP-Downloads, die sich überlappen, scheitern mit `ESP_ERR_HTTP_CONNECT`. Also nacheinander und mit 1,5 s Abstand (Y:1393-1419).

**Sicherheit:**
- `/local` ist ohne Anmeldung erreichbar, und HA hat die externe URL `https://dein-name.duckdns.org` (Bericht ha).
- Besser: Die Integration liefert die Datei über einen `HomeAssistantView` mit `requires_auth`. Die Remote schickt dann `request_headers: Authorization: Bearer !secret …` mit (HTTP/__init__.py:271).

**Alternative: HA schiebt die Daten in Stücken über einen API-Dienst**
- Dienst `remote_cfg_chunk(seq,total,hash,data)` mit `supports_response` für die Quittung je Stück.
- Funktioniert nur, solange die Remote verbunden ist. HA bräuchte eine Warteschlange und erneutes Senden beim Reconnect.
- Ein Frame über 32 768 B beendet die API-Verbindung (API/api_frame_helper.h:31). Stücke deshalb ≤ 8-16 KB.
- Ein `string`-Argument ist ein `StringRef` in den Empfangspuffer und muss sofort kopiert werden (laut Bericht transport: API/__init__.py:108-128).

**Nicht empfohlen:**
- **SD-Karte:** Pin-Konflikt mit dem Mikrofon, kein automatisches Einbinden, und der Reload braucht SD (Y:345-350, 2559-2597).
- **Konfig-Modus (HTTP) vom Browser:** CORS und gemischte Inhalte, nur im LAN, braucht SD (laut Bericht transport: `sd_http.cpp:71-72`).
- **`text:`-Entitäten:** maximal 255 Zeichen (Y:6638-6655).
- **ESPHome-Preferences:** feste Größe.
- **HA-Link:** entfernt.

### 5.3 Wo eingehängt wird

| Zweck | Ort |
|---|---|
| Laden beim Boot | `setup()` der neuen Komponente; Werte der BLE-Option bei on_boot `-100` (Y:292-296) |
| Abgleich und Meldung an HA | `api: on_client_connected` (Y:605-642) |
| Neue Dienste | `api: services:` (Y:645-757), neben `remote_reload_config` (Y:729) |
| Hash-Abo | `text_sensor:` (Y:6656ff) |
| Download | `http_request:` (Y:1349-1357), nacheinander mit `online_image` (Y:1393-1419) |
| Schlaf und WLAN-Aus sperren | `wartung_bis` / `ota_aktiv` (Y:1249-1251) |
| Ausführung | `tca8418: on_key` (Y:6069ff) → Header-Funktion. Ersetzt `gesture_action` (Y:4872) und `dispatch_key` (Y:4549), wenn eine Konfiguration geladen ist. |
| YAML-Änderungen | nur in Y. Remote 2 wird daraus erzeugt (`tools/sync_wohnzimmer_from_couch.py`). |

### 5.4 Hosting in Home Assistant (Handy und PC)

**Bestand:**
- HA 2026.9.4 (`/config/.HA_VERSION`).
- `panel_custom` enthält nur Iframe-Verknüpfungen für Admins (`/config/configuration.yaml:117-153`).
- Pakete werden über `/config/configuration.yaml:592` geladen (`packages: !include_dir_named packages`).
- Es gibt kein pyscript, AppDaemon und kein Node-RED.
- Benutzer laut Bericht ha: Remote 1, Chris und Heinz sind keine Admins.
- Etwa 7723 Entitäten, darunter 291 Skripte und 333 `input_boolean`.

**Empfehlung: eigene Integration `/config/custom_components/esphomeremote_konfig/`**
- **Panel:** registriert sie selbst, oder ein Paket `/config/packages/esphomeremote_konfigurator.yaml` mit:
  ```yaml
  panel_custom:
    - name: esphomeremote-konfigurator
      url_path: fernbedienung
      sidebar_title: Fernbedienung
      sidebar_icon: mdi:remote
      module_url: /local/esphomeremote/konfigurator.js?v=1   # ?v= bei jeder JS-Änderung erhöhen (App-Cache)
      embed_iframe: false        # Panel bekommt das hass-Objekt
      require_admin: false       # Remote 1 ist kein Admin
  ```
  Ob sich die Liste mit `configuration.yaml:117` zusammenführen lässt, ist ungetestet.
- **Bedienung:** Das Panel läuft in der Companion-App und im Desktop-Browser, im LAN und über duckdns.
- **Speichern:**
  - `helpers.storage.Store` in `/config/.storage/esphomeremote_konfig`.
  - Websocket-Befehle `esphomeremote_konfig/get` und `/save` ohne Admin-Pflicht. Das Panel ruft sie mit `hass.callWS` auf.
  - Die Integration prüft das Schema, schreibt die Gerätedatei (siehe 5.2) und setzt die Hash-Entität.
  - Nach dem Speichern ruft das Panel `esphome.remote_wz_<name>_remote_config_check` auf. Ist die Remote offline, wird der Fehler ignoriert; der Abgleich kommt dann beim nächsten Connect.
- **Entitätsauswahl:**
  - `hass.states` liefert die Liste: durchsuchbar, nach Domäne gefiltert (`script`, `input_boolean`, `light`, `switch`, `scene`, `cover`, `media_player` …).
  - Die passenden Aktionen je Domäne kommen aus `hass.services` mit Feldern: `turn_on`, `turn_off`, `toggle` und für Skripte `script.turn_on`.
- **Schnelle Erstversion:** `shell_command` + Base64 + Python-Skript, das über eine Temp-Datei und `os.replace` schreibt. Vorbild ist `/config/packages/esphomeremote_smarttube.yaml:274-281`. Grenzen: etwa 95 KB JSON (128 KiB je Argument), und jeder HA-Benutzer darf den Dienst aufrufen.
- **Nicht geeignet:**
  - `input_text` (255 Zeichen)
  - `frontend/set_user_data` (je Benutzer, die Remote kann es nicht lesen)
  - `frontend/set_system_data` (vermutlich nur für Admins, ungetestet)
- **Später:** Der geplante Designer für Touchscreen-Seiten passt in dieselbe Integration.

### 5.5 Vorschlag für das Datenmodell (Eingabe für Konfigurator und Laufzeit)
```json
{ "v": 1,
  "slots": ["Google Streamer", "Sony TV", "Tablet WZ", "frei"],
  "activities": [
    { "name": "Streamer", "slot": 0,
      "keys": { "33": { "short": {"t":"ble","target":"active","kind":"consumer","code":233,"hold":true} },
                "23": { "short": {"t":"ha","svc":"light.toggle","data":{"entity_id":"light.hangelampe"}},
                        "double":{"t":"ha","svc":"scene.turn_on","data":{"entity_id":"scene.wohnzimmer_tv"}},
                        "long":  {"t":"ha","svc":"light.turn_off","data":{"entity_id":"light.wohnzimmer"}} },
                "31": { "short": {"t":"int","fn":"menu_smart"}, "double":{"t":"int","fn":"display_off"},
                        "long": {"t":"int","fn":"menu_top"} } } } ] }
```

**Aktionstypen:**
- `none`
- `ble` mit `target` = `active` oder Slot, `kind` = `consumer` / `key` / `button` / `system`, `hold`
- `ir` mit `proto` + `addr` + `cmd` oder `raw[]`, `freq`
- `ha` mit `svc` und `data`
- `int` mit `fn` = `menu_smart` / `menu_open` / `menu_top` / `home` / `display_off` / `voice_ptt` / `assistant_toggle` / `activity:n` / `clean` / `power_off` …
- `seq`: Schritte mit `delay_ms`, umgesetzt nicht blockierend über `set_timeout`, **nicht** mit `delay:` aus `execute_action`

**Kopieren:** Eine Aktivität kopieren ist eine tiefe Kopie im Panel. Die Vorbelegung des Panels ist die heutige Belegung aus Abschnitt 2.2.

---

## 6. Risiken und Grenzen

1. **Internes RAM ist knapp.**
   - Etwa 27 KB frei (Y:515-518, README.md:408). Bei 7 KB konnte die Remote ihre Entitätsliste nicht mehr an HA senden (Y:524-526).
   - Mit `CONFIG_SPIRAM_USE_MALLOC=y` und `ALWAYSINTERNAL=512` (Y:514, 520, 527) landet jede Allokation unter 512 B intern.
   - Daraus folgt: keine `std::string` oder `std::vector` je Aktion (216 Plätze). Die Ausnahme sind kurzlebige Strings beim Senden.
   - JSON nur mit `SpiRamAllocator` parsen. Die Tabelle liegt in einer PSRAM-Arena.
   - Das Datenmodell von `runtime_config` (RCH:43-76) ist hier das Gegenbeispiel.
2. **`setup()` liegt nahe an der l32r-Grenze.**
   - Gemessen im Build vom 2026-10-01: `setup()` ist **0x340d5 = 213 205 B** groß (`xtensa-esp32s3-elf-nm -S` auf BUILD/build/firmware.elf). l32r reicht 256 KB.
   - Die YAML warnt mehrfach: Y:7296-7300, Y:7387-7388, Y:12509-12510, Y:3031-3034, Y:2412, Y:10663-10664.
   - Daraus folgt: Laufzeit und Executor als C++-Komponente oder Header. Keine neuen YAML-Widgets oder Lambdas in Callbacks von LVGL-Widgets. Den großen `on_key`-Lambda eher verkleinern, indem er in einen Header ausgelagert wird.
3. **WLAN aus, Tiefschlaf und API-Latenz.**
   - WLAN geht aus nach `ps_wifi_off_s` (Vorgabe 7200 s, Y:1156; `ps_wifi_off` true, Y:1120). Bei schwachem Akku sofort (`wifi_ruhe_timer` Y:1824-1843).
   - Tiefschlaf nach `ps_deep_s` (3600 s, Y:1161).
   - Die API ist erst etwa 9,1 s nach dem Boot verbunden (README.md:400). Nach „WLAN aus“ ist die IP in 0,06 s da (README.md:401).
   - Daraus folgt: Die Konfiguration muss auf dem Gerät liegen. HA-Aktionen brauchen die Warteschlange aus 3.4. `wlan_frueh` (Y:1135, Vorgabe true) schaltet das WLAN zwar beim Tastendruck ein, der Aufruf im selben Moment geht aber heute verloren.
   - Der Tastendruck, der aus dem Tiefschlaf weckt, wird verworfen (TCA:145-151).
4. **Pin-Konflikt zwischen SD und Mikrofon** (Y:345-350). Nach dem Einbinden der Karte ist das Mikrofon bis zum Neustart tot, und Sprache ist gesperrt (Y:3916-3920). SD kommt weder als Speicher noch als Transport für Tasten in Frage. Das Anlernen von IR schreibt heute auf SD.
5. **BLE an andere Geräte.**
   - Senden geht nur an den aktiven Slot.
   - `switch_host` schreibt NVS und gibt gehaltene Tasten frei. Es gibt höchstens einen geparkten Slot (KB:2293-2413).
   - `activity` und der aktive Slot können auseinanderlaufen (siehe 2.4).
   - „Beliebiges Gerät“ braucht eine Erweiterung im Fork (3.2).
   - Weitere Grenzen: `CONFIG_BT_ACL_CONNECTIONS "4"` (Y:502); Sprache hängt an der aktiven Verbindung (atvv_park/unpark).
6. **Latenz und Semantik der Gesten.**
   - DOPPELT verzögert KURZ um 160 ms. Mit LANG oder DOPPELT gibt es kein Halten mehr.
   - Der gemeinsame Gestenzustand macht zwei Tasten kurz nacheinander unsauber (4.6).
   - Der Tasten-Test kann hängen bleiben (4.3).
7. **Blockierende Stellen.**
   - `delay:` in `execute_action` blockiert die Schleife (KB:3712-3717).
   - Die Aktion `http_request` ist synchron.
   - Ein NVS-Schreibvorgang bremst Flash und PSRAM-Cache.
   - Daraus folgt: Folgen von Aktionen über `set_timeout` ausführen.
8. **API-Frames ≤ 32 768 B** (API/api_frame_helper.h:31). Gilt nur für den Push-Weg.
9. **Fehleranfälligkeit der Aktionsgrammatik.**
   - Ein unbekannter Text wird getippt (KB:3937).
   - `power` heißt System Power Down, `ok` heißt Enter, `color_*` heißt F1-F4.
   - Der Konfigurator sollte deshalb nur strukturierte Aktionen erzeugen und keine freien Strings.
   - `ha_action:` braucht `ha_action: true` (KB:4025-4028). Die Empfehlung ist der direkte Aufruf `call_homeassistant_service`.
10. **Sicherheit.** `/local` ist ohne Anmeldung erreichbar, auch über die externe URL. Abhilfe ist ein authentifizierter View (5.2).
11. **Zwei Geräte.** Remote 2 wird aus Remote 1 erzeugt. Dateinamen, Dienstnamen (`esphome.remote_wz_<name>_…`) und die Hash-Entität hängen an `App.get_name()`.
12. **Feste Ausnahmen** (4.7): Einschalten über 102, Reinigung beenden mit 13 lang, Touch-Test mit 13, Weck-Regel heute auf 31.
13. **Veralteter oder toter Code**, der beim Umbau aufzuräumen ist:
    - `case 102` in `dispatch_key` (Y:4580, Y:4622) wird nie erreicht.
    - Der Menü-Tap-Block bei Y:4712-4728 wird nie erreicht.
    - `case 31` bei Y:6191 wird nie erreicht.
    - Die Repeat-Fälle bei Y:6223-6225 werden nie erreicht.
    - Die Kommentare „41 nur Startseite“ (Y:4933, Y:6104-6105) und „long_press für 41“ (Y:4592-4595) sind veraltet.
    - Ebenso „0 = TV“ (Y:1011-1012) und „BLE-Platz 1/2“ (Y:2850-2851).