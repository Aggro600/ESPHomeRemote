# Open Remote — SD-Karte + Konfigurator-Kompatibilität (Weg A)

Stand 2026-09-09. Separate Testversion, **die funktionierende no-SD-Firmware
(`esphomeremote-test-5-ui.yaml`, Gerät `remote-wz-wohnzimmer`) bleibt unangetastet.**

- SD-Testversion: `esphomeremote-sd-test.yaml`, Gerätename **`remote-wz-wohnzimmer-sd`**
  (eigenes Build, eigenes OTA-Ziel — kann die no-SD nicht überschreiben).
- Neue Custom-Components: `components/sd_card/`, `components/runtime_config/`.

## Ziel (Remote 2-Vorgabe)
- Kompatibel zum **Dateiformat** des Original-Konfigurators
  (github: LORDSn1per/OpenRemote-Firmware → `runtime.json`).
- **Die vorhandene LVGL-Menü-Bedienung bleibt.** Keine dynamische GUI aus der JSON.
- ESPHome bleibt.
- Nicht dabei: das eingebettete WebConfig (Webserver + SPA), die dynamische
  Seiten-/Theme-/Icon-Engine, die IRDB (100 MB) — siehe „Weg B" im Chat.

## Wie das Original wirklich arbeitet (Recherche 2026-09-10)
Quelle: `github.com/LORDSn1per/OpenRemote-Firmware` (Ordner `remote/`, `sd-card/`,
`studio/`, Releases mit `OpenRemote-Studio-2.79`).

- Die **100-MB-IRDB liegt NICHT auf der Fernbedienung.** `sd-card/README.md`:
  „`irdb`: optional IR database files" + `studio/.../irdb/.keep.txt`: „The full
  database is not copied to the remote."
- **OpenRemote Studio** (Desktop) hat die DB. Man sucht dort sein Geraet, Studio
  **loest die Codes auf** und schreibt pro Geraet eine **`.ir`-Datei im
  Flipper-Zero-Format** nach `/devices/<Name>.ir` auf die SD (+ `/devices/index.txt`).
- Format:
  ```
  Filetype: IR signals file
  name: Power
  type: parsed | raw | pronto
  protocol: NEC | NECext | NEC42 | SIRC | SIRC15 | SIRC20 | RC5 | RC6 |
            Samsung32 | Kaseikyo | Pioneer | RCA | JVC
  address: 04 00 00 00      # Flipper-Hex, Little-Endian-Bytes
  command: 08 00 00 00
  frequency: 38000
  data: <us-Timings>  bzw.  <Pronto-Hex-Woerter>
  ```
- Die Firmware hat nur einen `.ir`-Parser + Protokoll-Encoder (fuer die 4
  „Extra"-Protokolle Kaseikyo/NEC42/Pioneer/RCA einen Raw-Encoder in
  `shared/openremote_ir_extra_protocols.h`).

**Fazit:** keine Datenbank noetig — ein Flipper-`.ir`-Parser + ~12 Encoder.

## Umgesetzt (Stand 2026-09-10)

`components/runtime_config/ir_encoders.h` — Raw-Encoder (LSB/MSB-Pulse-Distance,
Manchester), alles zu us-Timings: NEC, NECext/NEC1, NEC42, Sony SIRC 12/15/20,
RC5/RC5X, RC6, Samsung32, Kaseikyo/Panasonic/Denon, Pioneer, RCA, JVC + Pronto.
`components/runtime_config/ir_flipper.h` — Flipper-`.ir`-Parser
(`parse_flipper_ir(content, out)`), host-testbar.

`runtime_config.cpp`:
- `load_ir_device_files_()` liest `/devices/index.txt` bzw. scannt `/devices/*.ir`
  → je Datei ein `Device`, jeder Befehl zu `CMD_IR_RAW` (Timings vom Encoder).
- Laeuft auch **ohne** `runtime.json` (nur `.ir`-Dateien reichen).
- `runtime.json`-Befehle: `ir.type` "raw" / "pronto" / "parsed" werden ebenfalls
  aufgeloest; `fileBacked`-Geraete + Namensdubletten uebersprungen.
- Unbekanntes Protokoll → `CMD_IR_DB` + Zaehler `irdb_unresolved_` (anlernen).

`components/menu_ui/menu_ui.cpp`:
- **`build_from_runtime_json_()`** — ohne `menu.json` wird das Menue aus dem
  **Studio-Export** gebaut: `devicePages` → je Geraet eine Seite (4-Spalten-
  Raster, Items an ihrem `slot`), `pages` (Aktivitaeten + eigene Seiten,
  `settings` uebersprungen), `macros` (gemeinsame Registry mit Aktivitaeten),
  `themes` → Seitenfarben aus `colour1/2/3`. Buttons ueber Geraete-/Befehlsnamen
  (trifft `.ir`- und runtime.json-Geraete trotz verschiedener ID-Schemata).
- **`build_auto_menu_()`** — Fallback ohne `runtime.json`: direkt aus
  `runtime_config` (nur `.ir`-Dateien reichen).
- **`studio_icons.h`** — 63 OpenRemote-Studio-Iconnamen (`volume_up`, `ok_select`,
  `streaming_app` …) → mdi. `icon_cp_()` probiert mdi-Name, normalisiert, dann
  Studio-Tabelle.
- Rebuild ueber `rc_->add_on_load_callback`. Config: `runtime_path`,
  `wallpaper_swap`.
- **Wallpaper** (`theme.runtimePath` → `.rgb565`, 240x320): `load_wallpaper_()`
  liest die Datei per `sd_->read_binary()` in einen PSRAM-Puffer, baut ein
  `lv_image_dsc_t` (RGB565) und haengt es via `lv_image_create` +
  `lv_obj_move_background` hinter alle Menue-Widgets. `apply_page_theme_` schaltet
  `dyn_root` bei aktivem Bild transparent. Voraussetzung: `LV_USE_IMAGE=1` als
  Build-Flag (`esphome: platformio_options: build_flags` in der YAML) - ESPHome
  aktiviert das LVGL-`image`-Widget sonst nur bei einem `image:` im `lvgl:`-Block.
  Bei falschen Farben `menu_ui: wallpaper_swap: true` (Datei-Byte-Order vs.
  Display `big_endian`).
- **`settings` aus `runtime.json`/Studio** (`RcSettings`): `runtime_config` parst
  `brightness` (0-255), `sleep`/`sleepTimeout` (s), `wakeSensitivity` (0-2),
  `name`, `timezone`, `city` und feuert nach `load()` die `on_load:`-Automation.
  In der YAML uebernimmt ein `on_load:`-Lambda Helligkeit → `disp_bri` (%),
  Timeout → `disp_timeout_s`, Weck-Empfindlichkeit → `motion_mode`, dann
  `script.execute: apply_brightness`.

**Tests (host, `scratchpad/ir_test.cpp` + `flipper_test.cpp`, g++):** Encoder-
Frames byte-fuer-byte gegen Referenz (NEC 04/08, SIRC12, Samsung32 07/E6); echte
Flipper-IRDB-Dateien (LG/Sony/Samsung) → 9/9, 13/13, 44/44 Befehle aufgeloest;
`build_from_runtime_json_`-Logik gegen `omote_runtime.json` durchgespielt
(4 Geraeteseiten + Aktivitaetsseite, Themes aufgeloest). **ALLE OK.**
compile_wp (Wallpaper + settings + IR-Haertung, voller IDF-Rebuild):
**RAM 58.6 % Flash 26.5 %, sauber**, `firmware.factory.bin` + `.ota.bin` erzeugt.

**Noch offen (kosmetisch / bewusst):**
- Zellengroesse/-abstand nicht pixelgenau wie Studio.
- `settings`-Seite: wir haben unsere eigene (die Werte selbst werden uebernommen).
- Wallpaper-Byte-Order auf echter Hardware pruefen (`wallpaper_swap`).
- On-device-Test (Remote 2 + SD-Karte).

## Workflow fuer den Nutzer (kurz)
1. **OpenRemote Studio** (Desktop, aus den GitHub-Releases) oeffnen.
2. Geraete aus der IRDB waehlen, Aktivitaeten/Makros/Seiten/Theme bauen.
3. Export auf die SD-Karte: schreibt `/runtime.json`, `/devices/*.ir` (+ `index.txt`),
   ggf. `/themes/*.rgb565`.
4. SD in die Fernbedienung, „Menue (SD) oeffnen" (bzw. „Config: neu laden").
   Ohne eigene `menu.json` baut die Firmware das Menue direkt aus dem Studio-Export.

**Unterstuetzte IR-Protokolle** (Encoder → us-Timings): NEC, NECext, NEC42,
NEC42ext, Sony SIRC 12/15/20, RC5/RC5X, RC6, Samsung32, Kaseikyo/Panasonic/Denon,
Pioneer, RCA, JVC, Pronto sowie `type: raw` direkt. Unbekanntes Protokoll →
anlernen (IR-Learn) oder in Studio als `.ir` exportieren.

**Grenzen:** `irc_…`-Befehle ohne Timings/`.ir` werden nicht gesendet; Menue-
Raster nicht pixelgleich zu Studio; eigene `settings`-Seite (Werte werden aber
uebernommen).

**Konfigurator:** kein neuer noetig. **OpenRemote Studio** (Desktop) bleibt das
Werkzeug: Geraete aus der IRDB waehlen, Aktivitaeten bauen → schreibt SD →
unsere Firmware liest alles.

## Rev6 microSD-Hardware (netlist-verifiziert)
| Signal | Pin | |
|---|---|---|
| SD_SCK | GPIO15 | **geteilt mit I2S-Mic** (R45 4k7) |
| SD_MOSI | GPIO17 | **geteilt mit I2S-Mic** (R46 4k7) |
| SD_MISO | GPIO7 | **geteilt mit I2S-Mic** (R47 4k7) |
| SD_CS | GPIO18 | |
| SD_EN | GPIO16 | Q7 P-MOSFET, **LOW = Karte an** |
| SD_DET | — | über TCA8418 ROW6, nicht am ESP-GPIO |

→ SD und Sprachmikrofon sind **exklusiv**. In der Testversion: „SD mounten"
übernimmt die Pins → Mic bis Neustart tot. Für den Test okay.

## `components/sd_card/`
SPI-Modus über ESP-IDF (`esp_vfs_fat_sdspi_mount`, eigener SPI-Host SPI3 → kein
Konflikt mit ESPHomes `spi:`). ESPHome schließt `fatfs` normal aus →
`include_builtin_idf_component(fatfs, wear_levelling, sdmmc, esp_driver_sdspi)`
+ LFN (lange Dateinamen).

Config: `clk_pin/mosi_pin/miso_pin/cs_pin` (reine GPIO-Nummern → keine
„pin doppelt"-Kollision mit dem I2S-Mic), `power_pin` (GPIO16), `mount_point`,
`frequency`, `mount_on_boot`.

API: `mount() / unmount() / is_mounted()`, `card_size_bytes / free_bytes /
card_name / card_type`, `list_dir(path) / file_exists / read_text / write_text /
remove_file / make_dir`.

HA-Buttons: SD: mounten / auswerfen / Wurzel auflisten / Schreibtest / Lesetest.
Textsensoren: SD Status / SD Info / SD Inhalt.

## `components/runtime_config/`
Liest `/sd/runtime.json` (Filter-Parse mit ArduinoJson 7 → nur devices/commands/
activities, spart Heap). Baut Registry:
- Devices: id/name/type/transport/protocol
- Commands: **IR raw** (`ir.data` = µs-Timings) → über `remote_transmitter` (ir_tx,
  GPIO5, aktiv-LOW, Q5/D2). **HID** (`hid.report` keyboard/consumer, `hid.usage`)
  → über `espidf_ble_keyboard` (kb). **IRDB** (`irc_…` + protocol, kein Code) →
  Phase 2 / via IR-Learn.
- Activities: steps[] (command / delay) → asynchron via `set_timeout`-Kette.

API: `load()`, `send_command(devId,cmdId)` / `send_command_by_name(devName,cmdName)`,
`run_activity(id)` / `run_activity_by_name(name)`, `device_names / command_names /
activity_names / devices_json`, `last_result()`.

HA-Services: `esphome.remote_wz_wohnzimmer_sd_remote_send_command {device, command}`,
`…_remote_run_activity {activity}`, `…_remote_reload_config`.
HA: `text.…_testbefehl_geraet/befehl` + Button „Config: Testbefehl senden".
Textsensoren: Config Status / Config Geraete.

## runtime.json → auf die Karte
Vorerst: Konfigurator (Studio/WebConfig) → `runtime.json` exportieren → per PC
auf die SD-Karte (`/runtime.json`). Upload-Endpunkt am Gerät = Phase 2.

## IR-Learn (fertig, in runtime_config + sd-test.yaml)
`remote_receiver` GPIO4 (TSSP77038-OUT, aktiv-LOW), IR-VCC GPIO6 (`ir_rx_power`).
Ablauf: HA-`text` `Testbefehl Geraet`/`Befehl` setzen → Button „Config: IR lernen
scharf" (schaltet IR-RX-Strom an, 30 s scharf) → echte FB draufhalten, Taste
druecken → `on_raw` → `rc->learn_capture(dev, cmd, 38000, timings)` schreibt nach
`/sd/learned.json` und mergt sofort in die Registry (matcht IRDB-Befehl per id/Name;
unbekannt → Sammel-Geraet „Gelernt"). `runtime_config` liest `learned.json` bei
jedem `load()` und ueberschreibt die passenden IRDB-Befehle.

## `components/menu_ui/` — DECISION Weg 3: volles datengetriebenes Menue
Liest `/sd/menu.json`. Erzeugt LVGL-Widgets zur Laufzeit als Kinder von `dyn_root`
(LVGL-Seite `page_dyn`, Titel `dyn_title`). Bei jedem Seitenwechsel wird `dyn_root`
geleert und neu aufgebaut.

**menu.json** (Beispiel: `esphomeremote-docs/menu.sample.json` → als `/menu.json` auf
die SD): `{schemaVersion, start, theme{bg,card,accent,text}, pages[{id,name,cols,
items[]}]}`. `item.type`: `nav`(target) · `button`(device+command | activity |
press:"back"/"home" | ha_service+entity) · `light`(entity → Slider, Helligkeit) ·
`cover`(entity → Positionsbalken, Klick=stop, D-Pad ←/→ = zu/auf) · `toggle`(entity
→ HA toggle) · `label`(entity → Zustandstext) · `spacer`. `slot` = Rasterposition
(row-major, `cols` pro Seite), `span` = Zellen breit.
`item.icon` (optional) = mdi-Name aus dem Katalog
`esphomeremote-docs/menu-icon-katalog.txt` (153 Icons, Font `mdi_menu`,
Tabelle `components/menu_ui/mdi_icons.h`). Unbekannt/leer → kein Icon, Layout
bleibt. Konfigurator-Feld `iconName` wird auch akzeptiert.

**HA rein:** `api::global_api_server->subscribe_home_assistant_state(entity, attr, cb)`
(brightness / current_position / state). **HA raus:** Trigger `on_ha_service`
(service, entity) + `on_ha_number` (entity, service, value) → YAML
`homeassistant.service` (version-robust).

**D-Pad (fertig):** `menu_ui` hat eigenen Fokus (`focus_move(dx,dy)` naechste
Zelle nach Bildschirm-Naehe, `focus_activate()` = OK). In `dispatch_key` bei
`cur_page == 99` route: Hoch/Runter/Links/Rechts → `focus_move`, OK →
`focus_activate`, Zurueck → `go_back()` bzw. am Wurzelknoten `go_home` (raus auf
`page_home`), Menue-Taste → raus. Lautstaerke/Mute/Transport/Power gehen weiter
per BLE-HID an den Fernseher. Halten wiederholt die Pfeiltasten (`key_repeat`).
Links/Rechts auf einem `light`/`cover`-Regler aendert den Wert (in `focus_move`
abgefangen), genau wie in der fest verdrahteten Bedienung.

**Navigations-Einhaengung:** `page_dyn` ist eine ZUSAETZLICHE Seite, die die
bestehende (fest verdrahtete) Bedienung nicht anfasst. Einstieg: HA-Button
„Menue (SD) oeffnen". `on_load` setzt `cur_page = 99`, ruft `menu->reopen()`
(nav_stack leeren, Startseite) + `focus_move(0,0)`. Verlassen per Zurueck/Menue
am Wurzelknoten → `page_home`.

Button „Menue (SD) oeffnen" springt zu `page_dyn`. „Config: neu laden" laedt
runtime.json + menu.json neu.

## Konfig-HTTP-Endpunkt (Remote 2-Entscheidung: „Option 2" = nur im Konfig-Modus)
`components/sd_card/sd_http.{h,cpp}`. **KEIN Dauer-Webserver.** HA-Schalter
„Konfig-Modus (HTTP)" → Script `sd_konfig_start` (mode restart): mic aus, SD
mounten, `sd_->http_enable(0)`, `ota_aktiv=true` (kein Tiefschlaf), `delay: 10min`,
`sd_konfig_stop`. `sd_konfig_stop`: `http_disable()`, `ota_aktiv=false`,
`backlight_idle_timeout` neu anstossen. Schalter-`lambda` spiegelt
`sd_->http_active()` → geht nach 10 min sichtbar wieder aus.

`sd_card` bekam `AUTO_LOAD=["web_server_base"]` + `DEPENDENCIES += ["network"]`.
`http_enable()` registriert den Handler EINMAL (`http_registered_`), ruft
`global_web_server_base->init()` (Refcount 0→1, Listener an); `http_disable()`
`deinit()` (1→0, Listener aus). Laeuft auf der Stations-IP (bzw. Fallback-AP-IP).

Endpunkte (Query `?path=/menu.json`, Pfad muss mit `/` beginnen, kein `..`):
| | |
|---|---|
| `GET /sd/api/info` | `{mounted,name,type,size,free}` |
| `GET /sd/api/list?path=/` | `[{name,dir,size}]` |
| `GET /sd/api/file?path=…` | Dateiinhalt (text/plain) |
| `POST /sd/api/file?path=…` | Body schreibt die Datei — **Content-Type NICHT `application/x-www-form-urlencoded`** (sonst greift der Header-Size-Limit-Pfad); `text/plain` / `application/json` / `octet-stream` gehen. Chunk-weise via `handleBody` → `write_text(append)` |
| `DELETE /sd/api/file?path=…` | loeschen |

Sicherheit: im 10-min-Fenster im LAN offen (kein Auth). Optionaler `?key=`-Check
liesse sich in `SdHttp` nachruesten.

Beispiel: `curl -T menu.json -H 'Content-Type: application/json' \
'http://<remote-ip>/sd/api/file?path=/menu.json'`

## Noch offen
1. **IRDB**: `irc_…`-Befehle (Protokoll ohne Timings, kein `.ir`-Export) werden
   nicht gesendet. Workaround: in Studio als Flipper-`.ir` exportieren oder am
   Geraet anlernen. Eigene 100-MB-DB = kein Ziel.
2. **Rasterfeinheiten**: Zellengroesse/-abstand nicht pixelgenau wie Studio;
   `settings`-Seite ist unsere eigene (die Werte selbst werden uebernommen).
3. **Host-Test fuer `build_from_runtime_json_`**: die JSON→Seiten-Logik ist fast
   LVGL-frei, aber die Structs haengen an `menu_ui.h`/LVGL. Ein sauberer Host-Test
   braucht ein Struct-Split — bewusst zurueckgestellt (Risiko an funktionierendem
   Code vor dem Hardware-Test). Bisher: Trockenlauf gegen `omote_runtime.json`.
4. **On-device-Test**: SD-Karte, Buttons, IR an echtem Geraet, Menue-Rendering,
   Icons, D-Pad, Wallpaper-Farben, Konfig-HTTP.

## Erledigt in dieser Runde
- **Icons** (`item.icon`): 153-Icon-mdi-Katalog, Font `mdi_menu`, `make_icon_()`
  in nav/button/light/cover/toggle. `mdi_icons.h` + `menu-icon-katalog.txt` aus
  `fonts/mdi-subset.ttf` generiert (fontTools).
- **menu_ui Fokus-Zeiger-Sicherheit**: `load_gen_`-Zaehler, jeder
  `subscribe_home_assistant_state`-Callback prueft `gen == load_gen_` und wird
  nach einem `menu.json`-Reload zur No-op (Callbacks lassen sich nicht abmelden).
- **cover D-Pad**: ←/→ = `cover.close_cover`/`open_cover` (grob), Klick = stop.
- **Konfig-HTTP-Endpunkt** (Remote 2 „Option 2"): `sd_http.{h,cpp}`, HA-Schalter
  „Konfig-Modus (HTTP)" → 10-min-Fenster, `/sd/api/{info,list,file}`, kein
  Dauerserver, kein Tiefschlaf im Fenster. `sd_card` AUTO_LOAD web_server_base.
- **Kompatibilitäts-Analyse** gegen echte `runtime.json` (s.o. „Kompatibilität
  zum Original-Konfigurator").

## Status
- [x] sd_card kompiliert + linkt (VFS-DIR-Fix)
- [x] runtime_config kompiliert (compile_sd4, RAM 58 % Flash 26 %)
- [x] IR-Sender + IR-Empfaenger + IR-Learn
- [x] menu_ui kompiliert + linkt (compile_sd6: `subscribe_home_assistant_state`
      war mehrdeutig → Lambda-Parameter auf `StringRef` umgestellt, RAM 58.4 %
      Flash 25.8 %)
- [x] D-Pad-Anbindung (`dispatch_key` p==99) + Navigations-Einhaengung
      (`page_dyn` als Zusatzseite, Button „Menue (SD) oeffnen")
- [x] Voller Build mit D-Pad-Route (compile_sd7)
- [x] Icons + Fokus-Zeiger-Sicherheit + cover-D-Pad (compile_sd8)
- [x] Konfig-HTTP-Endpunkt (compile_sd10: RAM 58.5 % Flash 26.1 %,
      `firmware.ota.bin` + `.factory.bin` erzeugt)
- [x] IR-Haertung: Encoder gegen Flipper-Firmware-Konstanten geprueft (5 Bugs
      gefixt: NECext-Komplement, SIRC20-Adresse, Samsung32/Pioneer-Timing,
      RCA-Bitfolge), RC5/RC6 inline, NEC42ext ergaenzt; Host-Tests gegen echte
      Flipper-IRDB-Dateien (LG/Sony/Samsung/Philips RC5/PhilipsTV RC6/RCA/
      Panasonic Kaseikyo/Sony-BR SIRC20) alle gruen
- [x] `settings` aus Studio/`runtime.json` uebernehmen (`RcSettings` +
      `on_load:`-Automation: Helligkeit/Timeout/Weck-Empfindlichkeit)
- [x] `.rgb565`-Wallpaper laden (`LV_USE_IMAGE=1` per Build-Flag,
      `sd_card::read_binary`, `lv_image` als Hintergrund)
- [x] Voller Build mit allen dreien (compile_wp: RAM 58.6 % Flash 26.5 %)
- [x] cover-Feinsteuerung: D-Pad ←/→ setzt Zielposition in 20-%-Schritten
      (Balken), nach 600 ms Ruhe → `cover.set_cover_position`; Enden = ganz zu/auf;
      Klick = stop. Sicht-Hinweis ▼ ■ ▲ in der Karte. `on_ha_number` verzweigt
      jetzt nach Service (`position` vs. `brightness_pct`). compile_cover:
      RAM 58.6 % Flash 26.5 %, sauber.
- [ ] OTA auf `remote-wz-wohnzimmer-sd` + on-device Test (braucht Remote 2 + SD-Karte:
      SD mounten, `/runtime.json` + `/menu.json` draufkopieren, Buttons, IR am
      echten Geraet, Menue-Rendering, Icons, D-Pad, Konfig-HTTP)
