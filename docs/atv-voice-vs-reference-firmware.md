# ATVV: `atv_voice` → OpenRemote‑Firmware 3.56 — offene Angleichungen

Referenz: `LORDSn1per/OpenRemote-Firmware` @ `49b6814` („Firmware 3.56 – keep
16kHz"), Datei `OpenRemote_1.0.ino` (Changelog oben, ATVV‑Code ab Zeile ~7700).
Die Zeilennummern unten beziehen sich auf diese Datei.

## Ausgangslage

`atv_voice` (mit `espidf_ble_keyboard`) macht **16 kHz mit Client‑Rollen‑MTU‑
Aushandlung (→ 210), DLE und 15‑ms‑Intervall** — also grob der Stand der
Referenz‑Firmware **3.52**. In Messungen kamen damit ~87 % der Frames durch.

Die Firmware hat seither (3.53 → 3.56) die restlichen Engpässe gefunden. Die
folgenden Punkte sind **noch nicht** in `atv_voice` — sortiert nach Wirkung.
Jeder Punkt ist isoliert umsetzbar und sollte einzeln auf der Rev6‑Hardware
getestet werden (Serial‑Log gegen die erwarteten Zeilen prüfen).

Wichtig: Firmware = NimBLE, `atv_voice` = Bluedroid. Die *Aufrufe* unterscheiden
sich, die *Logik* nicht. Bluedroid‑Äquivalente stehen jeweils dabei.

---

## P1 — Framing 160 B / 20 ms bei 16 kHz  (Firmware 3.52/3.54)

**Firmware:** `ATVV_FRAME_BYTES_16K = 160`, `ATVV_FRAME_INTERVAL_16K_MS = 20`
(Zeile 4069‑4070). 160‑Byte‑ADPCM = 320 Samples = 20 ms = **50 Notif/s**,
„exactly Google's preferred framing". `atvvChooseStreamFormat()` (7788).

**atv_voice heute:** `ATVV_FRAME_SAMPLES = 256` → 128 B → 16 ms → 62,5 Notif/s
(`atv_voice.h:100`). Kommentar dort begründet 256 gegen 40 — 320 wurde bisher
nicht probiert.

**Änderung:** `ATVV_FRAME_SAMPLES` 256 → 320, `ATVV_FRAME_PAYLOAD` → 160.
Der Audio‑Task rechnet die Frame‑Dauer aus `sample_rate_` — 320/16000 = 20 ms
ergibt sich automatisch. Puffergrößen (`ATVV_PCM_BUFFER_SAMPLES = 8192`) reichen.
Für den 8‑kHz‑Pfad die Framegröße MTU‑abhängig machen (siehe P4), sonst wird
8 kHz zu 40 ms/Frame.

**Test:** Log `ATVV format: ADPCM 16kHz, 160 byte frames every 20ms = 50 notif/s`.

---

## P1 — Nibble‑Reihenfolge  (Firmware 3.xx, Kommentar Zeile 8132‑8137)

**Firmware:** `frame[i] = (hi << 4) | lo` — erstes Sample eines Paares ins
**High**‑Nibble. Kommentar: die frühere Reihenfolge `low | (high << 4)`
vertauschte jedes Paar und „corrupts an IMA ADPCM stream at any rate".

**atv_voice heute:** `atv_voice.cpp:878` — `frame[i/2] = low | (high << 4)`.
Also die von der Firmware als falsch markierte Reihenfolge.

**Änderung:** eine Zeile: `frame[i/2] = (uint8_t)((low << 4) | high);` — wobei
`low` = erstes Sample, `high` = zweites (Namen dann anpassen, sonst irreführend).

**Vorsicht:** `atv_voice` wurde gegen Sony/Google‑Sniffs verifiziert und
lieferte „gute" Erkennung. Möglich, dass Googles Decoder tolerant ist oder das
Sniff‑Decode‑Skript dieselbe Reihenfolge nutzte. Erst nach P1‑Framing testen,
und wenn die Erkennung *schlechter* wird, zurücknehmen.

---

## P1 — CAPS_RESP‑Layout mit Model‑Byte  (Firmware 3.54, `atvvSendCapabilities` 7857)

**Firmware‑Paket:** `[CAPS_RESP, verHi, verLo, advertisedCodec, model,
frameHi, frameLo, 0, 0]`
- `advertisedCodec` = `0x03` (BOTH) wenn 16 kHz aktiv, sonst `0x01` (Zeile 7865)
- `model` = `0x00` on‑request / `0x03` hold‑to‑talk (Zeile 7859)
- `frameHi/Lo` = tatsächliche Framegröße (160 → `00 A0`)
- Log: `ATVV capabilities COMMITTED: version=1.0 model=… codec=0x03 frame=160`

**atv_voice heute:** `handle_get_caps_` (`atv_voice.cpp:262`) sendet 20 Byte:
`resp[3] = codec & 0xFF` (= `0x02`), `resp[4] = 0x03` (als „Konstante"
missverstanden — ist in Wahrheit das **Model‑Byte**, und die Google‑Original‑
Remote meldet dort `0x03` = hold‑to‑talk), `resp[6] = max_notify_len`.

**Änderung:**
- `resp[3]` = `0x03` wenn 16 kHz, sonst `0x01`
- `resp[4]` = Model. `atv_voice` startet den Stream selbst (kein MIC_OPEN‑Warten)
  → das **ist** hold‑to‑talk → `0x03` senden. Damit passt das gemeldete Modell
  zum tatsächlichen Verhalten.
- `resp[5] = frameBytes >> 8`, `resp[6] = frameBytes & 0xFF` (statt nur `[6]`)
- Rest 0. Länge 20 (Polsterung) ist ok, die Firmware polstert nicht, beide
  Remotes im Sniff aber schon.
- Log‑Zeile auf „ATVV capabilities COMMITTED: … codec=0x%02X frame=%u" bringen.

**Test:** genau diese Log‑Zeile, und danach `codec=0x03 frame=160` konsistent
mit der `ATVV format:`‑Zeile.

---

## P2 — Format‑Auswahl gegen die echten Link‑Parameter  (Firmware 3.51‑3.55, `atvvChooseStreamFormat` 7788)

**Firmware‑Gate für 16 kHz:** `hostWants16k && mtuFits(MTU ≥ 163) && dleOk &&
intervalOk(≤ 30 ms) && peerCarries16k && specVersion ≥ 0x0100`. Sonst 8 kHz,
und die `ATVV format:`‑Zeile nennt den Grund („MTU too small" / „no data length
extension" / „connection interval too slow" / „this peer could not carry it
before").

**atv_voice heute:** nimmt den konfigurierten Codec; `handle_get_caps_` fällt
nur zurück, wenn der Host den Codec gar nicht listet. MTU/DLE/Intervall gehen
nicht in die Entscheidung ein.

**Änderung:** In `handle_get_caps_` vor dem Senden der CAPS_RESP eine
`choose_stream_format_()` einführen:
- `mtu_` kennt die Komponente bereits (`atvv_on_mtu`)
- DLE‑Status: `espidf_ble_keyboard` muss ihn nach `atv_voice` durchreichen
  (neuer Hook `atvv_on_dle(bool)` — die Komponente ruft `esp_ble_gap_set_pkt_
  data_len` in `request_data_length_extension`, Zeile ~317, und bekommt das
  Ergebnis im GAP‑Event `ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT`)
- Intervall: `espidf_ble_keyboard` sieht es im
  `ESP_GATTS_..._UPDATE_CONN_PARAMS`‑Pfad bzw. `ESP_GAP_BLE_UPDATE_CONN_PARAMS_
  EVT` (Zeile ~558) — ebenfalls per Hook durchreichen (`atvv_on_conn_interval`)
- bei fehlendem 16 kHz automatisch `codec_ = ADPCM_8K`, `sample_rate_ = 8000`
  für diese Session setzen und in der Log‑Zeile den Grund nennen

---

## P2 — GET_CAPS verzögern bis MTU + DLE fertig  (Firmware 3.54, Zeile 69‑77)

**Firmware:** die CAPS_RESP wird bis zu **1500 ms** zurückgehalten, bis
MTU‑Exchange und DLE durch sind; danach wird mit dem verfügbaren Format
geantwortet. Grund: auf einem Reconnect schreibt der Host GET_CAPS *bevor* die
MTU oben ist — antwortet die Remote dann mit 8 kHz/20 B und wechselt danach auf
16 kHz/160 B, decodiert der Host gegen das falsche Format („nur bei Reconnects
kaputt").

**atv_voice heute:** `handle_get_caps_` antwortet sofort (synchron im
`atvv_on_write`).

**Änderung:** In `atvv_on_write` bei GET_CAPS nur `caps_request_pending_ = true`
+ Host‑Parameter merken; in `loop()` die CAPS_RESP senden, sobald
`mtu_ > 23 && dle_ok_` **oder** 1500 ms seit Empfang vergangen sind. MIC_OPEN /
MIC_CLOSE weiter sofort behandeln.

---

## P3 — Selbst‑Fallback auf 8 kHz mit Persistenz  (Firmware 3.55/3.56)

**Firmware:** Werden in einem 16‑kHz‑Stream > ¼ der Frames vom Controller
abgewiesen (`atvvFramesDroppedThisStream > 20 && > frames/4`, Zeile 8277),
zählt `atvv16kConsecutiveFailures` hoch. Bei **3** schlechten Streams in Folge
(`ATVV_16K_FAILURES_BEFORE_FALLBACK`, Zeile 5234) wird `atvv16kBad` in den
Preferences gesetzt und ab der nächsten Session 8 kHz genutzt. Ein sauberer
Stream setzt den Zähler zurück. „Forget pairing" löscht das Flag.

**atv_voice heute:** kein Drop‑Zähler, keine Persistenz.

**Änderung:**
- `dropped_this_stream_` mitzählen: in `send_audio_frame_` jeden endgültig
  fehlgeschlagenen Frame (nach den 8 Retries) zählen; **am Stream‑Start**
  zurücksetzen (nicht am Ende — Firmware‑Bug 3.56: „376 of 189 frames refused")
- am Stream‑Ende (`stop_streaming_`) die 3‑Strikes‑Logik + `ESPPreferences`
  (`global_preferences->make_preference<uint8_t>(...)`)
- beim Setup das Flag lesen; wenn gesetzt, `codec_`/`sample_rate_` auf 8 kHz

---

## P3 — Interaktionsmodell merken  (Firmware 3.49)

**Firmware:** das ausgehandelte Modell (on‑request / hold‑to‑talk) wird über
Reconnects und Reboots in den Preferences gehalten (`atvvModel`) und beim
Connect wiederhergestellt, statt auf jeder Verbindung auf on‑request
zurückzufallen. Ein Host, der auf einem warmen Reconnect kein GET_CAPS schickt,
lässt die Remote sonst auf ein MIC_OPEN warten, das nie kommt.

**atv_voice heute:** startet den Stream immer selbst (de facto hold‑to‑talk),
wartet nie auf MIC_OPEN. Damit ist dieser Bug praktisch schon umgangen —
**nur relevant, wenn P1 (Model‑Byte) eingebaut wird** und man danach doch auf
MIC_OPEN warten will. Fürs Erste: nichts tun, Notiz.

---

## P3 — Verbindungsprofil „voice" während des Streams  (Firmware, `requestBluetoothConnectionProfileMode` 7648)

**Firmware:** für die Dauer des Streams wird das kürzeste Intervall angefragt
(`BLE_PROFILE_VOICE` = 7,5–15 ms, Latency 0), danach zurück auf `RESPONSIVE`
(15–30 ms). `espidf_ble_keyboard` fragt bereits fest 7,5–15 ms an
(`request_host_friendly_conn_params`, Zeile 298) — für die Remote als reines
HID‑/Voice‑Gerät ist das dauerhaft ok, Batterie ist zweitrangig. **Kein
Handlungsbedarf**, außer man will später Idle‑Stromsparen.

---

## Reihenfolge für die Umsetzung

1. P1‑Framing (160/20) — größter Einzeleffekt, dann testen
2. P1‑CAPS_RESP‑Layout — Host decodiert dann gegen das echte Format
3. P1‑Nibble‑Reihenfolge — nur wenn 1+2 die Erkennung nicht schon gut machen
4. P2‑Format‑Gating + P2‑GET_CAPS‑Verzögerung zusammen (beide brauchen die
   neuen Hooks von `espidf_ble_keyboard`)
5. P3‑Fallback‑Persistenz
6. Rest nach Bedarf
