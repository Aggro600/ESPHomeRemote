# Funkkonzept „Dock, WLAN als Ausweg“ (2026-10-25)

Ersetzt den Bluetooth-Tunnel zu HA (HALINK über den Proxy des Docks). Der war ein Stromspar-Versuch und hat das Dock
überlastet: drei Dauerverbindungen über einen ESP32-C3 ohne PSRAM → Speicher zerstückelt → WLAN-Empfangspuffer
scheitern → Dock minutenlang offline (Nacht 24./25.10.).

Vorbild: OpenRemote-Original (github.com/LORDSn1per/OpenRemote-Firmware) – Remote und Dock sprechen ESP-NOW, das Dock
hält die Verbindung zu HA, der Funk der Remote ist nur bei Bedarf an; ohne Dock fragt die Remote über ihr eigenes WLAN.

## Wer funkt wann

| Situation | Weg |
|---|---|
| Taste, HA-Befehl, IR/433, Tastatur/Maus am Dock | ESP-NOW → Dock (sofort nach dem Aufwachen, WLAN aus) |
| Display an | alle 2 s Stand beim Dock abfragen (Player, Fernseher, Timer, Popup-Auslöser, Klingelbild, Konfig-Kennung, Wartung) |
| Cover, Klingelbild, Tasten-Konfiguration | HTTP über das Dock (typ 6–9), auch ohne eigene HA-Verbindung |
| Status, Log, Einstellungen an HA | über das Dock (`remote_status`, `remote_log`, `einst_melden`) |
| Fernseher/Streamer | über das Dock (`bt_weg` 2), eigene Bluetooth-Kopplung der Remote als Ausweg |
| Firmware-Update | HA setzt `w` im Dock-Stand → Remote öffnet das Wartungsfenster (WLAN 3 min) beim nächsten Abfragen |
| Sprache an den HA-Assistenten | eigenes WLAN bei Bedarf (bis 12 s Aufbau) |
| Kein Dock eingerichtet / Dock antwortet nicht | eigenes WLAN, solange das Display an ist (+30 s) |
| Display aus / Tiefschlaf | Funk aus; keine Popups (gewollt: „Popups nur bei Benutzung“) |

Bluetooth zu HA: keins (`ha_link_modus` 3 = Link gesperrt, wird gar nicht beworben).

## Bausteine

- `espidf_ble_keyboard`: `set_link_gesperrt()` – hält den HA-Link pausiert; `set_link_pause()` vor dem Bluetooth-Start nur merken.
- `esphomeremote-wlan.h`: `wlan_zugriff::bedarf(ms, grund)` / `bedarf_aktiv()` – zentrale WLAN-Anforderung.
- `remote-halink.yaml`: `tunnel.set_wifi_erlaubt(Wartung || bedarf_aktiv())`; `tk_cfg.laden_ueber_dock`; `tunnel.ohne_weg` → Bedarf.
- `remote.yaml`: Link-Sperre beim Start und alle 2 s; `tk_cfg.ha_wecken` → Bedarf; Dock-Ausfall bei Benutzung → Bedarf;
  Stand-Abfrage 5 s → 2 s; `w` im Dock-Stand → Wartungsfenster; Sprache an HA → Bedarf, Wartezeit 12 s.
- `tasten_konfig`: Konfiguration ohne HA-Verbindung laden, wenn `laden_ueber_dock()`.
- `ha_tunnel`: Haken `ohne_weg` (HTTP fand keinen Weg).
- HA `zustand.py`: `DockStand.wartung_anfordern/wartung_fertig` → `"w": 1`; `firmware.py`: Wartungsfenster über das Dock,
  wenn der Knopf „Wartungsfenster jetzt“ nicht erreichbar ist (bis 15 min warten).
- Konfigurator: Verbindungsarten „Dock, WLAN als Ausweg“ (empfohlen) und „Nur WLAN (ohne Dock)“; `ha_link_modus` 3 = „Nie“.

## Dock

Unverändert ESP-NOW-Zentrale + Bluetooth-Tastatur (4 Geräte) + IR/433 + Proxy (bleibt für andere HA-Bluetooth-Geräte).
Ohne die Tunnel der Remotes bleibt genug Speicher; ein Dock reicht für alle Remotes.
Schutz seit 2026-10-25: WLAN-Puffer begrenzt, kein Fallback-AP, `reboot_timeout: 1min`, Notbremse bei zerstückeltem Speicher.

## Nachbesserungen 2026-10-25 (Tag)

- Bedarf endet, sobald das Dock wieder antwortet; „WLAN verbindet sich“ zählt nicht als Dock-Ausfall (`funk_belegt`).
- Zuletzt erreichtes Dock bleibt vorn und wird im Flash gemerkt (`ok_pref_`); laut HA online Docks zuerst (Stand `do`).
- Tiefschlaf markiert eine neue Firmware als gut (sonst Rollback, wenn die Remote in den ersten 60 s einschläft).
- WLAN kommt: Remote meldet `esphomeremote_konfig.wlan_kommt` übers Dock, die Brücke verbindet HA sofort (statt 15-s-Takt).
- Sprache: wartet auf `voice_assistant.connected`, nicht nur auf die API.
- Popups: Benutzungsstatus (Display an oder Taste/Touch/Bewegung in der letzten Minute) → Stand alle 2 s; 0,8 s nach jedem
  Befehl übers Dock zusätzlich. HA hält kurze Auslöser (Skripte) 5 s im Dock-Stand (`DockStand._gehalten`).
- Status an HA nur bei Änderung, sonst alle 10 min; vor dem Tiefschlaf immer.
- Bluetooth-Notweg einstellbar (`bt_notweg`: 0 aus, 1 bis das Dock wieder antwortet); nicht mehr fest 10 min.
- Dock: blinkt bei jeder Taste (auch Bluetooth), nicht bei Hintergrundmeldungen; Blinken mit eingestellter Helligkeit.
- Firmware über das Dock auch, wenn HA den Knopf nach einem Neustart fälschlich als verfügbar zeigt.

## Offen / zu messen

- Akku: Stand-Abfrage alle 2 s bei Display an (Funk je Abfrage nur Millisekunden).
- Ohne Dock: Zeit bis zum ersten Befehl (WLAN-Aufbau).
- Kanalwechsel FRITZ!Box/Repeater: Remote merkt sich den Dock-Kanal, sucht sonst 1–13 ab.

## Zurück

Archiv-Kopien vom 2026-10-25 in `archive/`: `remote.vor_funkkonzept_…yaml`, `remote-halink.vor_funkkonzept_…yaml`,
`zustand.vor_funkkonzept_…py`, `firmware.vor_funkkonzept_…py`, `konfigurator.vor_funkkonzept_…js`. Verbindungsart im
Konfigurator zurück auf eine alte Einstellung (`ha_link_modus` 1/2) schaltet den Link wieder frei.
