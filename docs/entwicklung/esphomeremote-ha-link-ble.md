# HA-Link: parallele Bluetooth-Verbindung Remote ↔ Home Assistant (Versuch 2026-09-29 nachts)

Frage des Nutzers: Lohnt es sich, einen Bluetooth-Slot dauerhaft fuer Home Assistant zu nutzen, statt fuer
jede Kleinigkeit das WLAN zu wecken? WLAN wird eigentlich selten gebraucht.

## Kurzfassung

- **Technisch machbar.** Die Firmware kann 4 BLE-Verbindungen gleichzeitig; die Bluetooth-Komponente ist in
  einer Testkopie erweitert (zweite Verbindung neben Streamer/TV, ohne die HID-Logik zu stoeren) - kompiliert
  und laeuft auf die erste Remote Remote.
- **Live-Test in der Nacht NICHT gelungen - wegen Reichweite**, nicht wegen der Technik: der naechste aktive
  Proxy (Dunstabzugshaube) empfaengt die Remote am Ladeplatz nur mit **-95 dBm** (vorher -80 dBm, als sie
  woanders lag), der Flur-Proxy gar nicht. Drei Verbindungsversuche liefen in den Timeout.
- **Voraussetzung fuer den Dauerbetrieb: ein Bluetooth-Proxy im Wohnzimmer** (z. B. WZ IR, derzeit aus -
  ein ESP32 mit `bluetooth_proxy: active: true` reicht).
- **Stromersparnis realistisch bewerten:** WLAN kostet im Ruhezustand ~60-75 mA (Messung 2026-09-06:
  Konfig C ~150 mA → Konfig D "WLAN aus im Ruhezustand" ~75-90 mA). Das spart die Remote heute schon,
  indem sie das WLAN im Ruhezustand abschaltet. Der echte Gewinn des HA-Links ist daher weniger "mehr
  Laufzeit", sondern: **HA bleibt bei ausgeschaltetem WLAN erreichbar** - Klingel-Pop-up, Sleeptimer,
  Tastatur-Pop-up und Tasten-Aktionen (Haengelampe, Licht) funktionieren sofort, statt erst nach dem
  WLAN-Aufbau (2-5 s) oder gar nicht. Dadurch kann das WLAN viel frueher/aggressiver aus.
- Der Tiefschlaf (~1 mA) bleibt der groesste Hebel; eine BLE-Verbindung ueberlebt ihn nicht.

## Idee

Neben der HID-Verbindung zum Streamer/TV haelt die Remote eine **zweite BLE-Verbindung** zu Home Assistant.
HA erreicht die Remote ueber einen **ESPHome-Bluetooth-Proxy**, genau wie heute schon andere BLE-Geraete.

- **HA → Remote** (GATT-Write "CMD"): Klingel-/Sleeptimer-/Tastatur-Pop-up, Zustaende (Licht an/aus,
  Rollo, Bildmodus, Audioquelle ...)
- **Remote → HA** (GATT-Notify "EVT"): Tastendruck → HA-Aktion, Akkustand, Seitenwechsel usw.

WLAN nur noch fuer Bandbreite: Cover-/Kamerabilder, OTA, HA-Sprachassistent, Logs.

## Was gebaut wurde (nur Testkopie, NICHT in /config)

Gesichert unter `archive/halink_versuch_2026-09-29/` (Komponente, YAML-Diff, Testskripte) - die Aenderungen stehen unten, damit sie
reproduzierbar sind. Alle Stellen im Code sind mit `HALINK` markiert.

Komponente `espidf_ble_keyboard`:
- Eigener GATT-Dienst `7e1a0001-5c3d-4f2a-9e61-2d8b4c0a1f00`
  - `7e1a0002-...` CMD: write / write-without-response (HA → Remote), bis 180 Byte
  - `7e1a0003-...` EVT: read/notify (Remote → HA)
- Die Proxy-Verbindung wird an der **Adresse** erkannt (`add_link_peer("AA:BB:CC:DD:EE:02")`) und im
  CONNECT/DISCONNECT/WRITE-Ereignis vorab abgefangen: kein Host-Wechsel, keine Verschluesselung, keine
  MTU-Client-Rolle, kein Einfluss auf ATV-Voice.
- Solange ein HID-Host verbunden ist, wirbt die Remote **langsam** (500-1000 ms) weiter, damit sich der Link
  parallel verbinden kann; jede fremde Zweitverbindung wird sofort getrennt.
- Der Link fordert 500-1000 ms Verbindungsintervall an (stromsparend, Befehle kommen in < 1 s an).
- Befehle werden aus dem Bluetooth-Thread per Queue (mit Mutex) in den Hauptthread gereicht
  (`add_on_link_command`), Ereignisse gehen per `link_notify()`; `add_on_link_state` meldet Auf-/Abbau.

Test-YAML (on_boot): `ping<n>` → `pong<n>`, `akku` → Akkustand, `wlan_aus` / `wlan_an`,
jeder Tastendruck → `taste:<code>`.

Testwerkzeug `scratchpad/linktest.py`: spricht den Proxy mit `aioesphomeapi` an - exakt die Aufrufe, die HA
selbst macht (connect, GATT-Services lesen, Notify abonnieren, schreiben) und misst Ping-Laufzeiten.

## Ergebnis 2026-09-29 mittags: FUNKTIONIERT (Proxy CO2-Sensor Wohnzimmer)

Der CO2-Sensor im Wohnzimmer ist jetzt aktiver Bluetooth-Proxy (Remote dort mit -35..-43 dBm). Der HA-Link ist
in `/config` in die Komponente uebernommen (zusammen mit der Parallel-Option), Remote 1 laeuft damit.

| Pruefung | Ergebnis |
|---|---|
| Verbindungsaufbau ueber den Proxy | 6-8 s |
| HA -> Remote -> HA (ping/pong), Intervall 500-1000 ms | 20/20, Mittel **2,4 s** (zu traege) |
| HA -> Remote -> HA, Intervall **100-150 ms** | **30/30, Mittel 0,39 s** (min 0,15 / max 0,95 s) |
| Akku ueber den Link abfragen | ok ("87 %, 4,07 V") |
| Streamer-HID-Verbindung parallel | lief ungestoert weiter |

Stolpersteine, die gefunden und geloest wurden:
- **Langsames Intervall erst nach der Einrichtung** anfordern (beim CCC-Abo), sonst dauert die Dienstsuche des
  Proxys ueber eine Minute und er gibt auf.
- **Abgebrochene Verbindungsversuche** hinterlassen im Proxy haengende Zustaende (bekannter ESPHome-Fehler,
  esphome#19858) -> Proxy neu starten.
- Ein ESPHome-Proxy schickt Bluetooth-Antworten nur an **den** API-Client, der seine Werbung abonniert hat (HA).
  Testwerkzeuge muessen deshalb selbst abonnieren; HA selbst ist davon nicht betroffen.
- Ueber einen Proxy muss der Client den **CCC-Deskriptor selbst schreiben** (Notify allein reicht nicht).

## Messergebnisse der Nacht

| Pruefung | Ergebnis |
|---|---|
| Firmware kompiliert, laeuft auf Remote 1 | ja (Build 02:07 aus der Testkopie) |
| Remote sichtbar am Proxy Dunstabzugshaube | ja, -80 dBm (01:4x) bzw. -95 dBm am Ladeplatz (04:0x) |
| Remote sichtbar am Proxy Flur | nein |
| Verbindung Proxy → Remote | 3x Timeout (30 s) bei -95 dBm |
| Ping-Laufzeit, Stabilitaet neben HID | **offen** - braucht einen naeheren Proxy |

Nebenbefund: Nach jedem Flash schaltet die Firmware den Testmodus ("immer online") beim echten Neustart
aus (on_boot 100) - die Remote ging danach in den WLAN-Ruhezustand und war per WLAN nicht mehr erreichbar.

## Naechste Schritte (Vorschlag)

1. Proxy ins Wohnzimmer: WZ IR (ESP32) wieder anschliessen, `bluetooth_proxy: active: true` - oder die Remote
   fuer den Test naeher an die Kueche legen.
2. Test wiederholen: Laufzeit ping/pong, 1 h Dauerverbindung neben aktiver Streamer-HID-Verbindung,
   Tasten am Streamer muessen normal weiterlaufen.
3. Bei Erfolg HA-Seite bauen: kleine Custom-Integration (`custom_components/esphome_remote_link`), die ueber HAs
   Bluetooth-Stack die Verbindung haelt, Befehle als HA-Dienst anbietet und Ereignisse als HA-Events feuert.
   Alternative ohne HA-Code: ESPHome-Bruecke mit `ble_client` (z. B. WZ IR).
4. **Sicherheit** vor echten Befehlen (Tuer oeffnen!): Nachrichten signieren (HMAC mit gemeinsamem Schluessel
   + Zaehler gegen Wiederholung) oder den Link verschluesselt koppeln.
5. Danach Stromspar-Regeln anpassen: WLAN im Ruhezustand sofort aus, nur fuer Bilder/OTA kurz an.

## Offene Punkte / Risiken

- Proxy-Slots: ein ESPHome-Proxy hat 3 aktive Verbindungsplaetze; zwei Remotes belegen zwei.
- Die schwache Verbindung am Rand der Reichweite kann haeufige Abbrueche erzeugen → Proxy muss nah sein.
- Langsames Weiterwerben waehrend HID-Verbindung kostet etwas Strom (klein bei 1 s Intervall).

---

# Versuch 2026-10-05: ganze ESPHome-API ueber Bluetooth tunneln (Remote 1)

Idee: statt jede Funktion einzeln ueber ein eigenes BLE-Protokoll nachzubauen (57 HA-Sensoren, 120 HA-Aktionen,
~60 eigene Entitaeten), laeuft die **normale ESPHome-API** (inkl. Noise-Verschluesselung) durch den Link.

## Aufbau

- `components/espidf_ble_keyboard`: Link-Dienst `7e1a0001-...` (RX `...0002` write/write-nr, TX `...0003` notify),
  erkannt an der Bluetooth-Adresse des Proxys, an der HID-Logik vorbei (Marker `HALINK`). Ohne eingetragenen Proxy
  ist alles aus - Remote 2 ist davon nicht betroffen.
- `components/ha_tunnel` (neu): verbindet sich auf der Remote per **Loopback 127.0.0.1:6053** mit dem eigenen
  API-Server und reicht die Bytes durch. ESPHome selbst bleibt unveraendert. Rahmenformat in `ha_tunnel.h`.
- **Flusssteuerung** (Fenster + Quittungen in beide Richtungen): ohne sie verwerfen Remote und Proxy bei Stau Pakete
  ("ATT fixed channel already congested, drop PDU", Proxy: `esp_ble_gattc_write_char failed`). Erster Versuch ohne:
  nur 12,8 von 30 KB kamen an. Mit Fenster: verlustfrei. Zusaetzlich prueft die Remote vor jedem Notify
  `esp_ble_get_cur_sendable_packets_num` (Bluedroid meldet sonst OK und verwirft trotzdem).
- `components/network` (ESPHome 2026.9.1 + eine Zeile): `network::is_connected()` ist auch true, solange eine
  Tunnel-Sitzung offen ist - sonst wirft der API-Server bei WLAN aus alle Clients raus. **Nur in der Test-YAML.**
- `remote-wz-couch-halink.yaml`: bindet die erste Remote Datei unveraendert als Paket ein + Tunnel + Text-Sensor
  "Link Adresse" (aktuelle Werbeadresse, die braucht der Proxy).
- Gegenstueck zum Messen: `tools/halink_test.py` (verbindet ueber den Proxy co2-sensor-wz, misst, und bietet
  `127.0.0.1:16053` als Bruecke an - darueber verbindet sich ein normaler aioesphomeapi-Client wie HA).
  Proxy-Log: `tools/halink_proxylog.py`. Python-Umgebung: `pip install aioesphomeapi`
  (im SSH-Add-on mit `env -u PIP_EXTRA_INDEX_URL`, der HA-Wheel-Server haengt).

## Ergebnisse (Remote 1 im Dock, Proxy CO2-Sensor WZ, MTU 517)

| Pruefung | Ergebnis |
|---|---|
| Loopback 127.0.0.1 auf der Remote | geht (ESP-IDF-Standard) |
| Verbindungsaufbau Proxy -> Remote | 0,2-0,5 s bei schneller Werbung, 6-11 s bei langsamer (1-1,28 s) Werbung |
| API durch den Tunnel: Verbinden + Noise | 0,4-1,0 s |
| 61 Entitaeten + 16 Dienste auflisten | 0,4 s (schnelles Intervall) / 1,1-1,4 s (100-150 ms) |
| API-Anfrage hin+zurueck (device_info) | Median 104 ms (7,5-15 ms) / 307 ms (100-150 ms) |
| **WLAN aus, API ueber den Tunnel** | **12/12 Antworten, 110-225 ms, Zustaende kommen weiter** |

Durchsatz (20 KB, verlustfrei, Fenster 4-8 KB):

| Intervall | Remote->HA | HA->Remote | Ping Median |
|---|---|---|---|
| 7,5-15 ms | 16-17 KB/s | 17-19 KB/s | 170-180 ms |
| 15-30 ms | 12 KB/s | 11 KB/s | 155 ms |
| 30-50 ms | 8 KB/s | 6-9 KB/s | 225-245 ms |
| 100-150 ms | 5-6 KB/s | 3-6 KB/s | 270-360 ms |

Bilder (HA erzeugt sie schon fertig, `/config/www/esphomeremote_*.jpg`): 2-19 KB -> bei schnellem Intervall
0,15-1,1 s. Kamera-Einzelbild (~5-6 KB) ~0,4 s. Live-Kamera und Sprache weiter ueber WLAN.

RAM Remote 1 mit Tunnel-Firmware (WLAN an, ohne Sitzung): intern frei 34,8 KB, groesster Block 26 KB.

### Dauertest 2026-10-05 16:33-16:54 (20 min, WLAN aus, Intervall 100-150 ms)

- **240/240** API-Anfragen beantwortet, keine Trennung. Laufzeit hin+zurueck: min 193 / Median 277 / p95 546 / max 750 ms.
- RAM stabil: intern frei 30,2 KB, Minimum seit Start 26,5 KB, groesster Block 20 KB.
- Verbindungsaufbau nach Platzwechsel 10,8 s - die Remote warb da schon langsam (1-1,28 s). Fuer schnellen
  Wiederaufbau muss die Link-Werbung schneller sein (Strom gegen Reaktionszeit, noch zu messen).
- Parallel zur Streamer-HID-Verbindung NICHT getestet: Remote 1 ist am Streamer nicht gekoppelt (nur Remote 2).

## Offen / naechste Schritte

1. **HA-Seite**: kleine Integration (`custom_components/esphome_ble_link`), die ueber HAs Bluetooth-Stack
   (Proxy) verbindet, Flusssteuerung wie `tools/halink_test.py`, und `127.0.0.1:<port>` anbietet. Die
   ESPHome-Integration zeigt dann auf diesen Port. Besser: Bruecke waehlt selbst - WLAN-TCP, wenn erreichbar,
   sonst Tunnel. Braucht HA-Neustart und Umstellen des ESPHome-Eintrags -> mit Nutzer abstimmen.
2. Intervall automatisch: schnell (7,5-15 ms) bei Display an / Bildern, langsam bei Ruhe.
3. Bilder ueber den Tunnel: `online_image` will HTTP ueber WLAN. Variante: HA schickt das JPG als API-Aktion an die
   Remote (Daten in Stuecken) oder ein eigener Bildkanal im Tunnel.
4. `network`-Override nur so lange, bis es in ESPHome einen sauberen Weg gibt - bei ESPHome-Update neu abgleichen
   (die Datei stammt aus 2026.9.1). Nebenwirkung: waehrend einer Tunnel-Sitzung glauben alle Komponenten an
   "Netzwerk da" (http_request usw. versuchen es dann und scheitern) - pruefen.
5. Paralleltest mit HID-Host (Remote 2 oder Remote 1 am Streamer koppeln), Ruhestrom (Remote 2), Wiederaufbau nach Tiefschlaf.
6. Sicherheit: der API-Schluessel (Noise) verschluesselt den Tunnel Ende-zu-Ende - kein eigenes HMAC noetig.

### Stand 2026-10-05 ~17:40: Paralleltest (HID + Link) noch offen

- Remote 1 per ADB am Streamer gekoppelt (Zubehoer-Suche, "Remote Remote 1" ausgewaehlt) - HID-Verbindung lief.
- Erster Paralleltest scheiterte: Proxy-Verbindung err=133, und die HID-Verbindung zum Streamer riss ziemlich genau
  beim Link-Versuch ab (17:18:39). Ursache noch unklar. Gefunden: neben einem HID-Host warb die Remote gar nicht
  -> Waechter in `loop()` eingebaut (alle 2 s Werbung anstossen, wenn der Link fehlt).
- Werbedaten enthalten jetzt (nur mit Link) Herstellerdaten 0xFFFF "HL" + FNV-1a(Geraetename) - HA findet die Remote
  so unabhaengig von der Platz-Adresse. Remote 1: `ffff 484c a522f86e`.
- HA-Integration `custom_components/esphome_ble_link` geschrieben, aber NOCH NICHT aktiv (keine Konfiguration,
  kein Neustart, ESPHome-Eintrag von Remote 1 unveraendert auf 192.168.1.60).
- Test-YAML: mDNS aus (sonst stellt HA die Adresse per Zeroconf auf die WLAN-IP zurueck).

### Paralleltest HID + Link (2026-10-05 ~18:25) - geht

Drei Fehler gefunden und behoben, bis HID (Streamer) und Link sauber nebeneinander liefen:
1. **Keine Werbung neben einem HID-Host** -> Waechter in `loop()` (alle 2 s Werbung anstossen, wenn der Link fehlt).
2. **Notify-Verluste mit zwei Verbindungen**: `esp_ble_gatts_send_indicate` meldet OK, Bluedroid verwirft das Paket
   spaeter ("already congested, drop PDU", CONF-Status 0x81). Jetzt: immer nur EIN Notify unterwegs, Ergebnis aus
   `ESP_GATTS_CONF_EVT`. Wichtig: Status `ESP_GATT_CONGESTED` (0x8F) heisst "angenommen, Kanal jetzt voll" - das
   Paket geht raus. Erst als Fehler behandelt -> doppelte Pakete im API-Strom (Noise-Fehler).
3. **Steuerrahmen verworfen** (eigener Fehler in ha_tunnel): solange ein Datenrahmen wartete, gingen "Sitzung offen",
   Pong, Quittungen verloren; die WLAN-Meldung (0x21) wurde jede Schleifenrunde neu eingereiht (~30/s) und
   verstopfte den Link - daher die Haenger nach WLAN aus/an. Jetzt eigene Warteschlange, nichts wird verworfen.

Ergebnis danach: HID am Streamer + Link + API-Sitzung, WLAN aus: 12/12 API-Antworten (~305 ms), 12/12 HID-Tasten
(Link -> API -> HID -> Streamer), sauberes Ende nach WLAN an, Link danach gesund.
Remote 1 wurde dafuer per ADB am Streamer gekoppelt (`am start -n com.android.tv.settings/.accessories.AddAccessoryActivity`,
dann `input keyevent 23`).

### HA-Integration aktiv (2026-10-05 ~19:20, Remote 1)

- HAs ESPHome-Eintrag "Remote WZ" (Remote 1) per Reconfigure-Flow auf 127.0.0.1:16053 (Bruecke
  `custom_components/esphome_ble_link`, Konfiguration `packages/esphome_ble_link.yaml`). Zurueck: Reconfigure auf
  192.168.1.60:6053 + Paket entfernen. Backup: `.storage/core.config_entries.vor_halink`.
- Stolperstein: `BleakClientWithServiceCache` geht am HA-Proxy-Wrapper vorbei (BlueZ, `KeyError('path')`) ->
  `bleak.BleakClient` zur Laufzeit nachschlagen (HA ersetzt ihn) + Geraet ueber `async_ble_device_from_address`.
- Test ueber HA (`esphome.remote_wz_couch_wlan_test_aus`, 120 s): 19:21:00,21 "WLAN weg" -> WLAN-Sitzung beendet ->
  **0,3 s spaeter API-Sitzung ueber Bluetooth**. Alle 66 Entitaeten blieben verfuegbar; `ram_messen` aus HA kam
  ueber Bluetooth an, neue RAM-Werte in HA. Nach WLAN an bleibt die Sitzung im Tunnel (Wechsel zurueck erst bei
  der naechsten Verbindung).
- Remote 2: Test-YAML `remote-wz-wohnzimmer-halink.yaml` (kompiliert). Remote 2 schlaeft, Test sobald er erreichbar ist -
  WLAN bleibt bei Remote 2 an (Nutzerwunsch).

### 2026-10-05 ~19:50: Notaus-Schalter -> Remote 1 weg (offen)

- Bruecke kann jetzt zurueck auf WLAN: steht das WLAN 5 s stabil, wird die Tunnel-Sitzung beendet und die
  ESPHome-Integration verbindet ueber WLAN neu (~20 ms statt ~300 ms pro Anfrage). Noch nicht live getestet.
- Versuch "HA ueber Bluetooth"-Schalter (globals + `esphome: on_boot` priority 50 + Template-Schalter mit Neustart,
  in der Test-YAML). Nach dem Flash um 19:52 ist Remote 1 **komplett still** (kein WLAN, keine Werbung, Dock aus/an
  weckt nicht). Ursache unbekannt - aus der Ferne nicht zu sehen. Verdaechtig: der zusaetzliche `esphome: on_boot`-Block
  im Paket-Hauptteil. Schalter-Block deshalb aus beiden Test-YAMLs wieder entfernt; erprobter Stand vorkompiliert.
  Hintergrund-Waechter spielt ihn automatisch auf, sobald Remote 1 wieder im WLAN ist (Reset/USB vor Ort noetig).
- Remote 2: Dauertest (Streamer-HID + Link + API, WLAN bleibt an: `NOWLAN=1`) NUR nach Absprache - Nutzer will Remote 2 abends selbst nutzen.

### Remote 2 mit Tunnel-Firmware (2026-10-05 ~22:15), Kurztest

- `remote-wz-wohnzimmer-halink.yaml` geflasht (ohne Notaus-Schalter). HA spricht mit Remote 2 weiter direkt ueber WLAN
  (Remote 2 ist nicht an die Bruecke angeschlossen).
- Verbindungsaufbau Proxy -> Remote 2 (Akku, Streamer-HID verbunden, Sparmodi an) **unzuverlaessig**: oft Fehler 133 /
  HCI 0x3E ("Connection Failed to be Established"), sonst 4-32 s. Sparmodi aus (BLE verlangsamen, Prozessor drosseln)
  hilft etwas (0/6 -> 2/4 -> 3/4), erklaert es aber nicht allein. Remote 1 (Dock, Laden) verband in 0,4-4 s.
  Offen: Link-Werbeintervall neben HID (500-640 ms) verkuerzen? Proxy-Zustand (esphome#19858)?
- Wenn verbunden: 12/12 API-Antworten (~300 ms), 12/12 HID-Tasten an den Streamer, 61 Entitaeten, WLAN an.
- Testskript findet die Remote jetzt ueber die HL-Kennung (`halink_test.py "Remote Remote 2"`), versucht bis zu 5x zu
  verbinden und nutzt Port 16153 (16053 belegt die HA-Bruecke).

### Fix 2026-10-05 22:25: schnelle Link-Werbung nach Verlust -> Remote 2 6/6

- Ursache der Aufbau-Probleme bei Remote 2: neben einem HID-Host warb die Remote fuer den Link nur alle 500-640 ms,
  der Proxy scannt passiv mit geringem Tastverhaeltnis -> hoerte Remote 2 nur ~3x in 20 s. die erste Remote Boot-Schleife
  (Akku jetzt raus) war es nicht: auch ohne sie 1/6.
- Jetzt: 60 s nach einem Link-Verlust (und nach dem Start) wirbt die Remote alle 100-150 ms, danach wieder 500-640 ms
  (`LINK_ADV_FAST_MS`). HA baut den Link ohnehin sofort nach dem Verlust neu auf.
- Ergebnis Remote 2 (Akku, Streamer-HID, alle Sparmodi an, WLAN an): **6/6 Verbindungen in 2,1-7,0 s** (vorher 0-3/6,
  10-45 s). Danach Tunnel-Kurztest: Aufbau 2,6 s, 12/12 API (~300 ms), 12/12 HID-Tasten.
- Stromkosten der schnellen Phase noch nicht gemessen (nur 60 s je Verlust).

### 2026-10-05 22:40-23:10: Remote 2 an der Bruecke, Schalter "Nur Bluetooth" - Probleme

- Remote 2-Eintrag in HA auf 127.0.0.1:16054, mDNS bei Remote 2 aus. Bruecke verbindet jetzt fest ueber den eingetragenen
  Proxy (`proxy:` im Paket) - HA hatte sonst evtl. einen anderen Proxy gewaehlt, den die Remote als fremden HID-Host
  abweist.
- Link-Werbung ohne Link: 5 min schnell (100-150 ms), danach 250-300 ms (500-640 ms war zu selten, HA bekam keine
  Verbindung). Link zu Remote 2 danach in 12-32 s, aber gelegentliche Abbrueche mit 0x08 (Funk-Timeout).
- Schalter "Nur Bluetooth (WLAN aus)" (ha_tunnel `set_ble_only`, Rettung: 2 min ohne Link -> WLAN an).
  **Fehlschlag bei Remote 2:** HAs API-Sitzung durch den Tunnel kam nicht zustande ("Handshake timed out after 60 s"),
  deshalb erreichte auch "Nur Bluetooth AUS" Remote 2 nicht - und die Rettung griff nicht, weil der Link selbst stand.
  Remote 2 war ~8 min nicht erreichbar. Geloest, indem die Bruecke fuer Remote 2 den Proxy nicht mehr findet (Dummy-Adresse
  im Paket) -> Link weg -> Rettung -> WLAN -> Schalter aus. **Remote 2 laeuft jetzt nur ueber WLAN (Bruecke leitet durch).**
- Ursache des Handshake-Problems noch offen. Verdacht: der Schalter schaltet das WLAN in jeder Schleifenrunde aus,
  wenn Skripte der Remote es (Bewegung/Taste) wieder einschalten -> WLAN-Stack ackert dauernd; bei Remote 1 (Dock,
  keine Bewegung) lief der Tunnel bei WLAN aus problemlos.
- **Vor dem naechsten Versuch noetig:** Rettung an "keine API-Sitzung" statt "kein Link" koppeln; WLAN-Skripte der
  Remote im Nur-Bluetooth-Modus gar nicht erst einschalten lassen; Handshake ueber die HA-Bruecke bei Remote 2 mit
  Log nachstellen.
- Nachttest-Skript `tools/halink_nachttest.py` liegt bereit (startet automatisch bei TV aus, Ende 07:00 oder TV an,
  Bericht + Handy-Nachricht) - **noch nicht gestartet**, erst nach dem Fix.

### Fix 2026-10-05 23:50: "Nur Bluetooth" laeuft auf Remote 2

- **Ursache des Handshake-Fehlers:** `network::g_link_session` wurde erst gesetzt, nachdem `pump_socket_()` den
  Verbindungsaufbau bemerkt hatte. Der API-Server nimmt die Loopback-Verbindung aber sofort an und prueft in seiner
  naechsten Runde `network::is_connected()` - bei schon ausgeschaltetem WLAN flog der Client sofort raus. Bei Remote 1
  nie aufgefallen, weil die Sitzung dort immer bei WLAN an begann. Jetzt wird der Merker vor `connect()` gesetzt.
- Rettung: "Nur Bluetooth" beendet sich selbst, wenn HA 2 min ueber keinen Weg verbunden ist (vorher: nur Link
  geprueft). Schalter zeigt den echten Zustand (`lambda: id(tunnel).ble_only()`).
- WLAN-Einschalter der Remote (wlan_frueh_an, wake_backlight, Sparschalter) fragen `wlan_zugriff::g_nur_ble`
  (esphomeremote-wlan.h, in die erste Remote Vorlage; ohne Tunnel immer false). Wartungsfenster darf das WLAN auch bei
  "Nur Bluetooth" einschalten (`set_wifi_erlaubt`, OTA). Nach dem Boot laeuft ein Wartungsfenster -> WLAN ~3 min an.
- Remote meldet alle 10 s einen Status ueber den Link, die Bruecke schreibt ihn ins HA-Log ("Remote Remote 2 meldet").
- Ergebnis: WLAN aus, Sitzung ueber Bluetooth stabil, Text-Rundlauf HA -> Remote 2 -> HA ~350 ms.
- Nachttest gestartet 23:57 (`tools/halink_nachttest.py wohnzimmer`, Log `esphomeremote-docs/halink-nachttest-wohnzimmer.log`),
  wartet auf TV aus; Bericht `halink-nachttest-wohnzimmer-<datum>.md` + Handy-Nachricht.

### 2026-10-06 ~00:00-00:45: Anzeige, Wartungsfenster, Werbe-Merker - und Nachttest abgebrochen

- Kopfzeile: WLAN-Balken ohne WLAN-Verbindung immer dunkel (vorher alter RSSI + gruen bei HA ueber Bluetooth),
  Aktualisierung auch bei `wifi: on_disconnect`. Neuer gruener Punkt `ha_ble_dot` neben den Bluetooth-Balken =
  HA verbunden, aber nicht ueber WLAN (also ueber den Tunnel). In die erste Remote Vorlage, ohne Tunnel nie sichtbar.
- Wartungsfenster schaltet das WLAN jetzt auch bei "Nur Bluetooth" ein (`set_wifi_erlaubt` schaltet beim Fensterbeginn
  selbst ein - vorher kam der Tunnel dem 1-s-Abgleich zuvor und schaltete es sofort wieder aus).
- Werbe-Merker: jede Verbindung setzt `s_adv_running = false` (gerichtete Werbung meldet kein ADV_TERMINATED) -
  vorher warb Remote 2 nach einem Neustart mit Streamer-Verbindung gar nicht fuer den Link.
- **Offen:** Im Ruhezustand (Display aus, BLE verlangsamen + Prozessor drosseln an, Streamer verbunden) nimmt Remote 2
  keine Link-Verbindung an, obwohl er wirbt: 0/3 direkt, HA ebenfalls nur Timeouts. Bei Bedienung (22:26) 6/6.
  Verdacht: das Ruhe-Profil der HID-Verbindung (150 ms, Latency 3) laesst dem Controller keinen Platz fuer den
  Verbindungsaufbau. Naechster Schritt: im Ruhe-Profil fuer den Link-Aufbau kurz aufs schnelle Profil gehen bzw.
  Link-Intervall auf ein Vielfaches des HID-Intervalls legen; Messung mit BLE verlangsamen an/aus im Ruhezustand.
- Nachttest deshalb abgebrochen (haette Remote 2 mit Testmodus die ganze Nacht wach gehalten). Remote 2: Nur Bluetooth aus,
  Testmodus aus, HA ueber die Bruecke per WLAN.

### Loesung 2026-10-06 01:00: Remote nimmt den Link von allen HA-Proxys an

- Eigentliche Ursache der Aufbau-Probleme mit HA: HA waehlt den Proxy fuer die Verbindung selbst (bester Empfang:
  Dunstabzugshaube `AA:BB:CC:DD:EE:02` oder `AA:BB:CC:DD:EE:03`), die Festlegung in der Bruecke greift nicht. Die
  Remote kannte nur den CO2-Proxy und trennte die Verbindung als "zweite/fremde Verbindung" sofort (Remote 2-Log).
  Jetzt stehen alle drei in `proxy_macs` (Remote 2 + Remote 1), `proxy:` in der Bruecke entfernt.
- Achtung beim Testen: ein eigenes aioesphomeapi-Werkzeug, das Werbung beim Proxy abonniert, nimmt HA dieses Abo weg
  (HA "sieht" die Remote ueber diesen Proxy dann nicht mehr) - CO2-Eintrag in HA neu laden gibt es zurueck.
- Ergebnis: Link HA -> Remote 2 35 s nach HA-Start; "Nur Bluetooth" 6/6 Rundlaeufe (~650 ms), WLAN aus.
- Nachttest neu gestartet 01:10 (wartet auf TV aus).

### 2026-10-06: Nachttest bestanden + Bilder ueber Bluetooth

- **Nachttest Remote 2** (02:02-06:48, 4,8 h, nur Bluetooth, WLAN aus): **285/285** Rundlaeufe HA -> Remote 2 -> HA,
  Median 538 ms, p95 797 ms, 0 s Ausfall, kein Link-Abbruch, Streamer-HID durchgehend verbunden.
  Bericht `halink-nachttest-wohnzimmer-2026-10-06.md` (WLAN-Zeile korrigiert: der Sensor "WLAN verbunden" war der
  HA-Status -> umbenannt in "HA verbunden", neuer Sensor "WLAN aktiv").
- **HTTP-Vermittler** (`ha_tunnel: http_port: 8081`, `substitutions: ha_host: 127.0.0.1:8081`): alle HTTP-Zugriffe
  der Remote auf HA (Cover, Klingel-/Kamerabilder, Tasten-/Menue-Konfiguration) gehen an 127.0.0.1:8081. Ein
  eigener FreeRTOS-Task leitet bei WLAN direkt zu HA weiter, sonst durch den Tunnel (Rahmen 0x40-0x44, eigene
  Warteschlange im BTC-Task, `link_send` jetzt gesperrt/threadsicher). Die HA-Bruecke verbindet zu 127.0.0.1:8123.
  Eigener Task noetig: online_image ruft HTTP blockierend in der Hauptschleife ab.
  Waehrend einer Uebertragung schaltet der Link auf 7,5-15 ms, nach 5 s ohne HTTP zurueck.
- Ergebnis: WLAN-Weg ok; **Bluetooth-Weg: Cover 15,4 KB in 3,0-3,3 s, 12,6 KB in 1,5 s** (WLAN aus).

### 2026-10-06: HA-Sprachassistent ueber Bluetooth (ohne WLAN)

- ESPHomes voice_assistant schickt rohes PCM (32 KB/s) im verschluesselten API-Strom - zu viel fuer den Tunnel und
  unterwegs nicht komprimierbar. Deshalb eigener Audio-Kanal: Mikrofon -> IMA-ADPCM 16 kHz (~8 KB/s, Rahmen mit
  Startzustand, einzeln dekodierbar) -> Link -> HA-Bruecke dekodiert -> `assist_pipeline.async_pipeline_from_audio_stream`
  (Pipeline wie an der Remote unter "Assistent" gewaehlt). Ergebnisse zurueck (0x53: erkannt, Antwort, TTS-URL,
  Fehler, fertig) -> gleiche Anzeige/Lautsprecher-Ausgabe wie bisher (`on_voice_event` in der -halink-YAML).
  Codec-Test: 29,4 dB Stoerabstand.
- Mikrofon-Taste (die erste Remote Vorlage): HA verbunden + kein WLAN + Haken gesetzt -> Bluetooth-Weg ("hoert zu (Bluetooth)"),
  sonst wie bisher. Loslassen beendet den Audiostrom.
- Fehler unterwegs: (1) Anfang fehlte - 1-s-Puffer lief ueber, solange der Link noch im Spartakt war -> 6-s-Ringpuffer
  im PSRAM, 0 verworfen. (2) "Nur Bluetooth" ging nach Flash/HA-Neustart verloren - Rettung loeste bei HA-Neustart aus
  (jetzt: Uhr nur bei WLAN aus, 3 min) und der Template-Schalter mit Lambda stellt seinen Zustand nicht wieder her
  (jetzt speichert ha_tunnel ihn selbst).
- Ergebnis: "Wie ist das Wetter in Essen?" erkannt nach 4,1 s, Antwort (KI-Agent) nach 11,4 s; "Schalte die
  Haengelampe aus." erkannt + ausgefuehrt nach 4,3 s. Mehrfach hintereinander ok, 0 Rahmen verworfen.

### 2026-10-06: Menue-Schalter, Updates durch den Tunnel, lokaler Assistent

- **Menue:** "Nur Bluetooth" auf der Energie-Seite (unter "WLAN frueh einschalten"), nur sichtbar mit Tunnel
  (`wlan_zugriff::g_set_nur_ble`, gesetzt in der -halink-YAML). Zustand speichert ha_tunnel selbst (Preference).
- **Updates ohne WLAN:** HA-Bruecke lauscht auf 127.0.0.1:<ota_port> (Paket: `ota_port: 3232` bei Remote 2), das
  ESPHome-Add-on (Host-Netz) laedt mit `node tools/esphome_run.mjs upload remote-wz-wohnzimmer-halink.yaml 127.0.0.1` hoch.
  Remote: HTTP-Task verbindet per Loopback zum eigenen OTA-Server (Rahmen 0x60-0x64, eigener Task, weil OTA die
  Hauptschleife blockiert). **Ergebnis: 2,48 MB in 284 s (8,6 KB/s), OTA successful, WLAN die ganze Zeit aus**;
  danach "Nur Bluetooth" wieder an, Link + Streamer verbunden. (Schneller waere ein groesseres Fenster als 8 KB.)
  Achtung: nur eine Remote kann den Port 3232 auf dem HA-Host haben.
- **Lokaler Assistent** (Pipeline "Lokaler Assistent"): Whisper + Piper Add-ons gestartet und auf Autostart,
  Wyoming-Eintraege neu geladen, TTS von Google Cloud auf Piper (de_DE-thorsten-high) umgestellt. Damit komplett
  lokal: faster-whisper (de) -> Home-Assistant-Konversation (lokal) -> Piper. Test "Wie spaet ist es" -> Antwort
  + TTS nach 0,5 s.

### 2026-10-06: Parallel-Modus mit allen Hosts + HA-Link

- "4" war kein Hardware-Limit, sondern `CONFIG_BT_ACL_CONNECTIONS: 4` (die erste Remote Vorlage). Remote 2-halink: 6 Verbindungen
  (`CONFIG_BT_ACL_CONNECTIONS: 6`, `CONFIG_BT_CTRL_BLE_MAX_ACT: 8`) und `set_max_parked(2)` (vorher fest 1 geparkter
  Host - Kommentar von 09/2026: "mit zwei geparkten konnte sich das dritte Geraet nicht mehr verbinden").
- Sony-TV verband sich nach einem Trennen durch die Remote nicht mehr (auch ohne Parallel-Modus) -> am TV entkoppelt,
  "Kopplung loeschen" + "Koppeln: Sony TV", neu gekoppelt (Pairing Successful).
- **Ergebnis: HA-Link + Streamer + Tablet + TV gleichzeitig (4 Verbindungen)**, Wechsel reihum "ohne Neuverbinden",
  HA-Link ohne Abbruch.
- **RAM knapp:** intern frei 10,7 KB, Minimum 8,2 KB, groesster Block 7,5 KB (mit 2 Hosts ~20 KB). Im September gab es
  bei so wenig RAM Aussetzer (Mikrofon, HA-API). Beobachten; ggf. `set_max_parked(1)` (HA + 2 Hosts).
- **Nachtrag 12:10:** Mit 4 Verbindungen kam ab 12:04 keine Tunnel-Sitzung mehr zustande (Remote antwortete nicht
  auf 0x01, RAM ~8 KB). Die Rettung schaltete "Nur Bluetooth" nach 3 min aus -> WLAN -> HA wieder verbunden.
  Deshalb zurueck auf `set_max_parked(1)` (HA-Link + aktiver Host + 1 geparkter), `CONFIG_BT_ACL_CONNECTIONS: 5`.
  Frisch nach dem Start: 28 KB frei, groesster Block 20 KB.
- Menue: Knopf "Nur Bluetooth" wurde nie eingeblendet (apply_power lief vor dem Setzen des Hakens) -> nach dem
  Setzen und bei jeder Aenderung (auch durch die Rettung) apply_power ausfuehren. Text bei "Nur ohne Bluetooth"
  (Tiefschlaf): die Bluetooth-Verbindung zu HA zaehlt dabei nicht.
- **Menue (Konfigurator):** Die Remote zeigt die Einstellungsseiten des Konfigurators, nicht die eingebauten LVGL-Seiten -
  deshalb war "Nur Bluetooth" unsichtbar. `tools/einst_parse.py` + `einst_gen.py` neu laufen lassen (neue Eintraege
  `ps_nurble_btn`/`ps_nurble_h`, M_EINST in konfigurator.js nachgezogen) und in die gespeicherten Menues von Remote 2 und
  Remote 1 (Seite set_power) per `esphomeremote_konfig/save` eingefuegt. Der eingebaute Knopf bleibt als Fernsteuer-Ziel.
- **Geparkte Verbindungen** liefen mit dem schnellen Takt (15 ms) weiter -> Menue oeffnete mit Parallel-Modus spuerbar
  langsamer (Nutzer). Jetzt: beim Parken Ruhe-Profil (120-150 ms, Latency 3), beim Zurueckholen sofort schnell.
- **RAM (Remote 2-halink):** LVGL-Puffer 12,5 % statt 25 %, Stapel des Tunnel-HTTP/OTA-Tasks im PSRAM. Frisch nach dem
  Start: **38,8 KB frei / groesster Block 27 KB** (vorher 28,1 / 20 KB). Cover/Kamerabilder liegen schon im PSRAM
  (dekodiertes Bild, Download-Puffer, JPEGDEC) - abschalten wuerde internes RAM kaum entlasten.
- Energie bei "Nur Bluetooth": WLAN-Einstellungen wirkungslos; Tiefschlaf je nach TV ueber "Tiefschlaf bei TV an" = nie
  + "Tiefschlaf nach" (TV-Zustand kommt jetzt auch ohne WLAN aktuell ueber den Link).
- **Menue-Navigation (Konfigurator-Engine `components/menue`):** "Energie & Wecken" ist mit den Hinweistexten >128
  Rasterzeilen lang; `MAX_ZEILEN` war 128 -> die letzten 5 bedienbaren Eintraege lagen ausserhalb, uebereinander und
  unsichtbar (Fokus verschwand, unten nie erreichbar, "Zurueck" erst nach mehreren Druecken). Jetzt 256. Belegt per
  Fokus-Protokoll (`menue: Fokus ...`, Info-Log).
- **Updates durch den Tunnel brachen ab** (101 Bytes, dann Timeout), nachdem der Stapel des HTTP/OTA-Tasks ins PSRAM
  verlegt wurde: beim Flash-Loeschen zu Beginn des Updates ist PSRAM gesperrt. Stapel wieder intern (6 KB) - NICHT
  ins PSRAM verlegen. LVGL-Puffer 19 % (12,5 % ruckelte).
- Bluetooth-Seite (Konfigurator): Geraete-Knoepfe zweizeilig (Name + Status), einst_gen.py + gespeicherte Menues.
