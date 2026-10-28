# Architektur-Stand vor dem Umbau „Dock als Zentrale“ (2026-10-23)

Dieser Stand ist gesichert, damit wir zurückkönnen, falls der Umbau nichts taugt.

- **Archiv:** `/config/sicherung/vor-dock-zentrale-2026-10-23.tar.gz` (137 MB, enthält `secrets.yaml` – nur lokal, chmod 600)
  - `esphome/` (ohne Build-Cache `.esphome/`, ohne `archive/`)
  - `custom_components/esphomeremote_konfig/`, `custom_components/esphome_ble_link/`
  - `packages/esphome_ble_link.yaml`
  - `.storage/esphomeremote_konfig` (Konfigurator-Daten aller Remotes) und `.storage/esphomeremote_konfig_firmware`

## Geräte und Firmware-Stand

| Gerät | Chip | Firmware-Stand |
|---|---|---|
| Remote Remote 2 | ESP32-S3, Akku | 2026-10-23 ~16:00 (alles von heute) |
| Remote Gast, Remote Remote 1 | ESP32-S3, Akku | älter (ohne Heimnetz-Weg, ohne Tunnel-Wiederholung, ohne Dock-Takt) |
| Dock WZ Couchtisch | ESP32-C3, Netzteil | 2026-10-23 ~16:30 (Proxy-Takt-Override, 433-MHz-Puffer 2 kB) |
| Dock WZ Remote 2, Dock WZ Remote 1 | ESP32-C3 | offline, alte Firmware |

## Die drei Verbindungen einer Remote (Stand jetzt)

```
                 ┌─────────── WLAN (ESPHome-API direkt) ────────────┐
                 │                                                  ▼
  Remote ──── Bluetooth „HALINK“ ──► Proxy (Dock/Dunstabzugshaube) ──► Home Assistant
    │   │                                                           (esphome_ble_link: Tunnel auf 127.0.0.1:160xx)
    │   └── Bluetooth HID (eigene Kopplung je Remote) ──► Fernseher / Streamer / Tablet (4 Plätze)
    │
    └── ESP-NOW (Funk, ohne Router) ──► Dock ──► Infrarot, 433 MHz, HA-Befehle (über Dock-WLAN),
          (Ausweg bei WLAN an: UDP 6062 übers Heimnetz)    Bluetooth-Tastatur/Maus (dock_hid) ──► Streamer …
```

### 1. Remote → Dock: ESP-NOW (`components/dock_direkt`)
- Pakete mit HMAC (gemeinsamer Schlüssel `direkt_schluessel`), laufende Nummer gegen Wiederholung.
- Typen: 1 IR, 2 RF, 3 HA-Befehl, 4/5 Zustand fragen/antworten, 6–9 HTTP über das Dock (AES-CTR), 10/11 Bluetooth-Eingaben über das Dock (dock_hid).
- WLAN der Remote aus: Funk wird „geliehen“, Kanäle durchprobiert. `dock_kanal_` merkt den Kanal, auf dem das Dock zuletzt antwortete (NVS 0x44445203), `kanal_` den WLAN-Kanal (0x44445201).
- WLAN der Remote an: Senden auf dem WLAN-Kanal; bestätigt das Dock nicht → **UDP 6062 an die Dock-IP** (aus HA: `cfg.dip`), danach 5 min bevorzugt. Einstellung `dock_netz` (0 auto, 1 nur Funk, 2 Heimnetz zuerst).
- Frühversand beim Aufwachen (`set_frueh`, `pumpen`), Weck-Taste unter 1 s (`tasten_konfig::weck_frueh`, Einstellung `weck_dock`).

### 2. Remote → Home Assistant
- **WLAN:** ESPHome-API direkt (HA-Eintrag zeigt auf 127.0.0.1:Port der Brücke, die Brücke nimmt WLAN, wenn erreichbar).
- **Bluetooth „HALINK“:** Remote = Peripherie, HA verbindet über einen Bluetooth-Proxy (`custom_components/esphome_ble_link`). Tunnel für API, HTTP (Bilder), OTA, Sprache.
  - Heute dazu: Datenrahmen 0x03 mit Position + Wiederholung (Probe nach 1 s, sofort ab Lücke bei doppelter Quittung), Abbruch nach 6 s ohne Quittung, Wachhund 8 s ohne Antwort, Adress-Sperre `_ALLE`, Verbinden ohne Cache nach GATT-Fehler.
  - Remote fordert am Dock höchstens 30–50 ms Takt an (`espidf_ble_keyboard::set_link_langsam`, Docks = `dmac` + 2).
- **Über das Dock (ohne eigene HA-Verbindung):** HA-Befehle (typ 3), Bilder/Konfiguration (HTTP typ 6–9), Zustand (typ 4/5).

### 3. Remote → Geräte (Fernseher, Streamer …)
- Eigene Bluetooth-HID-Kopplung je Remote und Platz (4 Plätze, `espidf_ble_keyboard`), „Nur Tasten“ / Maus / Tastatur je Platz.
- Oder über das Dock (`dock_hid`, bis 4 Geräte je Dock): Zuordnung `cfg.bt.dock` (Remote-Platz → Dock-Platz), Einstellung `bt_weg` (0 automatisch, 1 nur Remote, 2 Dock zuerst). Maus/Tastatur gehen ans Dock, wenn der eigene Platz sie nicht hat.

## Dock (ESP32-C3) – alles auf einem Funkteil
- WLAN + ESPHome-API zu HA
- Bluetooth-Proxy (`bluetooth_proxy`, aktiv, 3 Verbindungen) – auch für andere HA-Bluetooth-Geräte
- `dock_hid`: Bluetooth-Tastatur/Maus für bis zu 4 Geräte (Dock = Peripherie)
- ESP-NOW-Empfänger + UDP 6062, IR-Sender, CC1101 (433 MHz senden/lernen)
- **Eigene Fassung** `components/bluetooth_connection` (ESPHome 2026.9.1): Proxy-Verbindungen mit 30–50 ms, solange ein Gerät per dock_hid verbunden ist. Ohne das lehnte der Controller jede Proxy-Verbindung ab („Rejected: Resources“). **Bei ESPHome-Updates neu abgleichen.**
- Speicher knapp: frei ~20–34 kB, Tiefstwerte 0,15–4,5 kB.

## Home Assistant (`custom_components/esphomeremote_konfig`)
- Konfigurator-Panel (PANEL_VERSION 99), Speicherung `.storage/esphomeremote_konfig`, Übertragung an die Remotes (Hash).
- `bruecke.py` (Links für esphome_ble_link), `dockscan.py` (Suchtempo der Docks), `dock.py` (IR-Geräte), `docks.py`, `firmware.py` …
- Konfiguration an die Remote enthält u. a. `dmac` (WLAN-MACs der Docks), `dip` (IPs der Docks), `bt.dock` (Zuordnung).

## Wichtige Einstellungen (Remote, Konfigurator ↔ `esphomeremote-einst-sync.h`)
`bt_weg`, `dock_netz`, `weck_dock`, `weck_tasten`, `ha_dock`, `http_dock`, `ha_link_modus`, `ha_link_ruhe`, `maus_slots`, `voll_slots`, Energiemodi (u. a. „Verbunden“).

## Bekannte Probleme dieses Stands
- Viele parallele Wege → schwer zu durchschauen, viele Fehlerquellen (heute: Zeitkonflikt Proxy/Streamer, verlorene Tunnel-Pakete, vertauschte Remotes, WLAN-Kanäle Box/Repeater).
- Dock-Speicher am Limit.
- Jede Remote muss jedes Gerät selbst koppeln.
- Wiederaufbau HA-Verbindung nach „WLAN aus“: 6–8 s (vorher 60–225 s).

## Test-Werkzeuge (als TEST markiert)
- `esphomeremote-logpuffer.h` + Knopf „Protokoll-Puffer ausgeben“ (Remote)
- Dock-Aktion `bt_trennen(platz, sekunden)`
- Messlauf `wlan_aus_test.sh` (Scratchpad): WLAN per `switch.remote_wz_wohnzimmer_nur_bluetooth_wlan_aus` aus, Zeit bis `ha_verbunden`

## Zurück zu diesem Stand
1. HA-Teile: `tar -xzf /config/sicherung/vor-dock-zentrale-2026-10-23.tar.gz -C /config custom_components packages .storage/esphomeremote_konfig` → HA neu starten.
2. Firmware: `tar -xzf … -C /config esphome` (überschreibt `esphome/`), dann `python3 tools/remotes.py`, je Gerät `node tools/esphome_run.mjs compile <cfg>` und `upload <cfg>` (upload baut nicht). Bei MDNS-Fehler: Build „clean“, neu bauen.
3. Konfigurator-Daten: `.storage/esphomeremote_konfig` nur bei gestopptem HA zurückspielen.
