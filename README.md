# ESPHomeRemote

**Eine universelle Fernbedienung auf Basis der OpenRemote-Rev6-Hardware
(OMOTE-Fork, ESP32-S3), vollstaendig in ESPHome — mit Touchdisplay,
Bluetooth-HID fuer mehrere Geraete, zwei Sprachassistenten und tiefer
Home-Assistant-Anbindung.**

Statt der urspruenglichen Arduino-Firmware laeuft hier ESPHome. Alles, was die
Hardware kann, ist ueber YAML konfigurierbar, und die Fernbedienung ist
zugleich ein vollwertiges Home-Assistant-Geraet.

## Top-Features

- **Menuebedienung komplett per Tasten** (D-Pad-Fokus mit Zeilen-/Spalten-Logik) — das kann das OMOTE-Original nicht, dort geht das Menue nur per Touch
- **Zwei Sprachassistenten** in einem Geraet: Google / Android TV *und* Home Assistant Assist, Umschalten per Doppeltipp, Antwort als Text auf dem Display
- **Bluetooth-HID mit 4 Host-Plaetzen**, je eigene Geraeteadresse — mehrere Streamer/TVs ohne dass sie sich gegenseitig zurueckholen
- **Aktivitaeten nach Harmony-Prinzip**: eine Aktivitaet waehlt Zielgeraet + BT-Host
- **Echter Rev6-Tiefschlaf**: gemessene ~1 mA, Wochen bis Monate Standby; Wecken per Taste, Hochheben (Lagesensor) oder Ladekabel
- **Vollwertiges Home-Assistant-Geraet**: steuert Licht/Rollos/Szenen/Multiroom/Kameras und meldet Akku, BT-Verbindung, WLAN und alle Schalter zurueck
- **Datenschutz-Mikrofonschalter**, der die Stromversorgung *physisch* kappt
- **Optionale microSD-Karte**: eigenes App-Logo auf der Startseite/Medienseite fuer Apps ohne HA-Metadaten (Netflix, Disney+ &c.), Menue/Geraete wahlweise von der Karte (OpenRemote-Studio-kompatibel)
- **Kamera-Integration**: Live-Bild und Klingel-Overlay direkt auf dem Display, auch aufs TV schaltbar
- **IR lernen**: eigene Fernbedienungscodes per IR-Empfaenger einlesen, ohne Cloud-Datenbank
- **YouTube-artige Zeitleiste** fuer laufende Wiedergabe, auf der Medienseite *und* als schlanker Balken direkt auf der Startseiten-Karte
- **WLAN-Selbstheilung**: erkennt eine haengende Verbindung zu Home Assistant selbst und repariert sie automatisch (Sparmodus neu setzen, notfalls WLAN neu starten)
- **Alles in ESPHome** — jede Funktion per YAML, keine Arduino-Toolchain

---

## Hardware

Diese Firmware laeuft auf der **OMOTE Rev6 / "Open Remote" Rev6** — ESP32-S3
(N16R8), 240x320-Touchdisplay, TCA8418-Tastenfeld, LIS3DH-Lagesensor,
MAX17048-Ladestandsanzeige, IR-Sender/-Empfaenger, microSD, I2S-Mikrofon,
Li-Ion-Akku.

- OMOTE-Projekt (Hardware + Original-Arduino-Firmware):
  https://github.com/OMOTE-Community/OMOTE-Hardware
- Es muss die **Rev6**-Platine sein (Pinbelegung, MOSFET-Rails und
  Weckpins sind fest verdrahtet auf diese Revision).

## Was sie kann

### Bedienung

- **Touchdisplay** 240x320 hochkant mit LVGL-Oberflaeche, durchgaengig eine
  Schrift (Audiowide, mit echten Umlauten) und ein Satz Groessen.
- **23 physische Tasten** ueber einen TCA8418. Jede Taste kann **kurz, lang und
  doppelt** unterschieden werden; der lange Druck loest bereits waehrend des
  Haltens aus, nicht erst beim Loslassen.
- **Zeilen-/Spalten-Navigation** mit dem D-Pad: Hoch/Runter wechselt die Zeile,
  Links/Rechts bewegt sich innerhalb der Zeile. Nebeneinanderliegende Knoepfe
  werden beim Runterdruecken uebersprungen, nicht durchlaufen.
- **Einheitliche Rueckmeldung**: Jeder Knopf faerbt sich beim Druecken blau —
  per Finger *und* per D-Pad. Der Fokusrahmen ist davon getrennt sichtbar.
- **Menue-Gedaechtnis**: Schliesst man das Menue und oeffnet es innerhalb von
  zehn Sekunden wieder, landet man auf derselben Seite. Langes Halten der
  Menuetaste geht bewusst an diesem Gedaechtnis vorbei und springt nach ganz
  oben.
- **Kopfzeile** mit Zurueck-Knopf, die beim Scrollen stehen bleibt.

### Aktivitaeten (Harmony-Prinzip)

Eine Aktivitaet bestimmt, **welches Geraet** das D-Pad steuert — und schaltet
dafuer den Bluetooth-Host mit um (z. B. eine Aktivitaet fuer den Streamer, eine
fuer den Fernseher). Umschalten per Knopf auf der Startseite; die aktive
Aktivitaet ist markiert. Welche Aktivitaeten es gibt, legt die Konfiguration
fest.

### Bluetooth-HID

- **Mehrere Host-Plaetze** mit getrennten Kopplungen und je eigener
  Geraeteadresse, sodass sich die Hosts nicht gegenseitig zurueckholen.
- **Koppeln, Verbinden und Loeschen je Platz**, im Menue und in Home Assistant.
- **Akkustand** wird an den Host gemeldet — die Fernbedienung erscheint dort
  mit ihrem echten Ladezustand.
- **Empfangsstaerke** als Balkenanzeige in der Statuszeile, daneben WLAN.
- **Kein Zeigegeraet, keine Tastatur** in der HID-Beschreibung. Das ist
  Absicht: Android wertet eine angeschlossene Tastatur als
  Konfigurationsaenderung und baut die Oberflaeche laufender Apps bei jedem
  Verbinden neu auf. D-Pad, OK, Zurueck, Home, Lautstaerke und Transport laufen
  ueber die Medientasten-Seite.

### Sprache — zwei Assistenten

- **Google / Android TV** ueber den ATVV-Dienst, 16 kHz ADPCM ueber BLE.
- **Home Assistant Assist** ueber die ESPHome-API. Die Fernbedienung erscheint
  in Home Assistant als vollwertiger **Assist-Satellit** mit eigener
  Pipeline-Auswahl.
- **Umschalten** durch zweimal kurzes Tippen auf die Mikrofontaste oder im
  Menue.
- **Antwort als Text auf dem Display** — die Fernbedienung hat keinen
  Lautsprecher. Man sieht, was verstanden wurde und was geantwortet wird.
- **Wahlweise ueber Zimmerlautsprecher**: Die fertige Sprachausgabe kann an
  einen Mediaplayer in Home Assistant weitergereicht werden.
- **Mikrofonstrom nur waehrend der Benutzung**, mit Vorlauf und Nachlauf, damit
  keine Silben abgeschnitten werden. Dazu ein Datenschutzschalter "Mikrofon
  stumm", der die Versorgung physisch kappt.

### Home Assistant

Die mitgelieferte Konfiguration enthaelt fertige Menue-Seiten fuer Licht,
Rollos, Szenen, Steckdosen, Ventilator- und Dunstabzugsstufen, Multiroom-Audio,
Klingelbilder, Kameras auf den Fernseher und Sleeptimer, nach Raeumen sortiert.
Diese Seiten sind als Beispiel gedacht und werden auf die eigenen
Home-Assistant-Entitaeten angepasst (siehe *An dein Zuhause anpassen*).

Die Fernbedienung selbst meldet nach Home Assistant: Akkustand,
Bluetooth-Verbindung samt Gegenstelle, WLAN-Zustand, Mikrofonschalter,
Assistentenwahl, Kopplungsknoepfe und einen Neustart-Knopf.

### Energie und Komfort

- **Display-Timeout** sekundengenau einstellbar, von fuenf Sekunden bis "Nie".
- **Zwei Weckarten, unabhaengig einstellbar**: *Hochheben* (Winkel gegen die
  Lage beim Ablegen, mit Haltezeit — Wackeln auf der Couch loest nicht aus)
  oder *Bewegung* (jede Handhabung), je mit eigener Empfindlichkeit.
- **"Auch beim Abdunkeln"**: dieselbe Bewegungs-/Hochheben-Erkennung hellt das
  Display schon auf, wenn es nur abgedunkelt (nicht ausgeschaltet) ist.
- **"Nicht abdunkeln bei Bewegung"**: solange die Fernbedienung gerade bewegt
  wird, wird das Abdunkeln aufgeschoben — praktisch, wenn man sie laenger in
  der Hand haelt. Eigene, unabhaengige Empfindlichkeit.
- **Tiefschlaf einzeln zuschaltbar**: aus = immer sofort erreichbar (mehr
  Verbrauch), an = nach eingestellter Zeit echter Tiefschlaf (Aufwachen dauert
  dafuer laenger, siehe *Top-Features*).
- **Helligkeit** fuer Display und Tastenbeleuchtung, zusaetzlich zwei
  gleich lange senkrechte Regler direkt auf der Startseite (links Tasten-,
  rechts Displayhelligkeit).
- **Statuszeile konfigurierbar**: Bluetooth, WLAN, Akku und Uhr einzeln
  abschaltbar. Die WLAN-Anzeige unterscheidet "WLAN da" (bernstein) von
  "WLAN *und* Home Assistant verbunden" (gruen).
- **Begruessungsbildschirm** nach jedem Start — eine verlaessliche Rueckmeldung,
  dass neue Firmware laeuft. Bleibt stehen, bis man sie aktiv wegtippt/
  wegdrueckt, auch nach einer laengeren Ruhephase.
- **Reinigungsmodus**: Tasten und Touch fuer einstellbare Zeit gesperrt, zum
  Abwischen des Displays. Start ueber Doppeldruck der Aus-Taste oder im Menue.

### Medien / Streaming

- **Ein Player fuer alles**: Titel/Interpret/Cover kommen wahlweise vom
  Streamer, vom Chromecast oder direkt vom Fernseher — je nachdem, was gerade
  laeuft. Laeuft etwas auf dem Fernseher, zeigt die Fernbedienung das.
- **YouTube-artige Zeitleiste** mit Restzeit, sobald eine Dauer bekannt ist —
  auf der grossen Medienseite mit Zeitangaben, auf der Startseiten-Karte als
  schlanker Balken unter dem Interpreten.
- **App-Erkennung ohne eigene Metadaten** (z. B. Netflix): zeigt das App-Logo
  von der SD-Karte, solange kein Titel bekannt ist, und automatisch die echten
  Metadaten, sobald welche kommen.

### SD-Karte (optional)

Mit bestueckter microSD-Karte (FAT32, Ordnerstruktur wie bei OpenRemote
Studio, siehe [`sd-karte/README.md`](sd-karte/README.md)):

- **App-Logos** fuer die Startseite und die Medienseite, eigene RLE-komprimierte
  Bilder (~5-13 KB je Cover), per `make_sd_assets.py` aus Simple-Icons (CC0)
  erzeugt.
- **Menue/Geraete von der Karte** (`runtime.json`, `menu.json`,
  `devices/*.ir`) statt fest in der Firmware — kompatibel zum Dateiformat von
  OpenRemote Studio.
- Karte und Mikrofon teilen sich Pins; das Mikrofon bleibt waehrend der
  Kartennutzung nutzbar (kein Neustart noetig).

### Kameras

- **Live-Bild** einer Home-Assistant-Kamera direkt auf dem Display, mit
  Weiterschalten zwischen mehreren Kameras.
- **Klingel-Overlay**: sobald der konfigurierte Klingel-Sensor "on" wird,
  kommt automatisch eine Seite mit Bild und "Tuer oeffnen".
- Kamerabild wahlweise auch **auf den Fernseher** legbar.

---

## Eigene Komponenten

| Komponente | Aufgabe |
|---|---|
| `ili9341_i80` | Display ueber den i80-Bus. Zeichnet je Bereich und wartet auf den DMA-Abschluss. **Nicht** auf asynchrones DMA oder Vollbild-Uebertragung zurueckbauen — genau das erzeugte Flackern und Bildfehler. |
| `espidf_ble_keyboard` | BLE-HID mit mehreren Host-Plaetzen, Bluedroid-GATTS. Optionen `pointer:` und `alpha_keyboard:` blenden Maus und Tastatur aus der Geraetebeschreibung aus. |
| `atv_voice` | Android-TV-Sprachdienst (ATVV) ueber BLE, ADPCM 16 kHz, mit weicher Begrenzung statt hartem Clipping. |
| `tca8418` | Tastenfeld, interruptgesteuert. |
| `lis3dh` | Lagesensor. Bewegung/Hochheben werden per Software aus den rohen Beschleunigungswerten erkannt (entprellt, mehrere Messungen in Folge) — der eingebaute Hardware-Interrupt loeste sich als zu stoeranfaellig heraus (feuerte auch bei stillliegendem Geraet) und wird nicht mehr fuer Aktionen ausgewertet. |
| `max17048` | MAX17048-Ladestandsanzeige (nur Spannung, kein Shunt) inkl. Laderate. |
| `open_remote_core` | Sammelheader (esp_pm.h, Deep-Sleep-RTC-Merker, Multiroom-Tabelle), den ESPHome vor die Lambdas einbindet. |
| `sd_card` | *(optional)* SPI-Treiber fuer die microSD-Karte inkl. HTTP-Konfig-Modus zum Hochladen von Dateien uebers WLAN. |
| `menu_ui` | *(optional)* Datengetriebenes Menue/Icons aus `menu.json`/`runtime.json` von der SD-Karte. |
| `runtime_config` | *(optional)* Liest die OpenRemote-Studio-kompatible Konfiguration (Geraete, Aktivitaeten) von der Karte. |
| `ir_learn` | Lernt Infrarot-Codes ueber den eingebauten IR-Empfaenger ein, ohne Cloud-Datenbank. |

## Ohne Home Assistant: Web-Installer

[![Web-Installer](https://img.shields.io/badge/Flashen-im%20Browser-22c55e)](https://aggro600.github.io/ESPHomeRemote/)

**[aggro600.github.io/ESPHomeRemote](https://aggro600.github.io/ESPHomeRemote/)**
— Fernbedienung per USB-C anstecken, im Browser (Chrome / Edge / Opera) auf
*Connect & Install*, danach das WLAN eingeben (Improv). Weder ESPHome noch
Home Assistant noetig.

Dieser Build (`open-remote-web.yaml`) nutzt neutrale Platzhalter-Zugangsdaten
und eine unverschluesselte lokale API. Fuer den Betrieb in Home Assistant den
Weg unten nehmen.

## Mit Home Assistant / ESPHome

[![In ESPHome oeffnen](https://img.shields.io/badge/Open%20in-ESPHome-000000?logo=esphome&logoColor=white)](https://my.home-assistant.io/redirect/esphome/)

**Der schnelle Weg (ohne das Repo zu klonen):**

1. In Home Assistant die **ESPHome**-Oberflaeche oeffnen → **Neues Geraet** →
   Assistenten abbrechen (nicht verbinden).
2. Beim neuen Geraet auf **Bearbeiten** und den kompletten Inhalt von
   [`open-remote.yaml`](open-remote.yaml) einfuegen. Oder ESPHome erkennt das
   Geraet im Netz und bietet **Adoptieren** an
   (`dashboard_import` zeigt auf `open-remote.yaml`).
3. In `secrets.yaml` die Werte aus `secrets.yaml.example` eintragen
   (WLAN, API-Key, OTA, AP).
4. **Speichern → Installieren** (erstes Mal per USB, danach OTA).

Firmware und die eigenen Komponenten werden dabei automatisch aus diesem Repo
geladen (`packages:` / `external_components: github://`), die Schriften per
`gfonts://`. Die Menue-Seiten fuers Smarthome an deine Entitaeten anpassen: die
`substitutions` oben in `open-remote.yaml` aendern (Details unten unter
*An dein Zuhause anpassen*).

## Einrichtung (lokal, zum Weiterentwickeln)

Voraussetzung: **ESPHome** (Home-Assistant-Add-on oder `pip install esphome`)
und dieses Repository lokal.

```bash
git clone https://github.com/Aggro600/ESPHomeRemote.git
cd ESPHomeRemote
cp secrets.yaml.example secrets.yaml     # ausfuellen (WLAN, API-Key, OTA, AP)
esphome run open-remote-firmware.yaml   # erstes Mal per USB, danach OTA
```

Die eigenen Komponenten werden ueber `external_components:
github://Aggro600/ESPHomeRemote` automatisch geladen — beim lokalen Klon
genauso wie beim Adoptieren ueber die ESPHome-Oberflaeche. Zum Entwickeln an den
Komponenten in der YAML auf die auskommentierte `type: local`-Zeile umstellen.

**In Home Assistant** nach dem Einbinden dem Geraet erlauben, HA-Aktionen
auszufuehren: *Einstellungen -> Geraete -> ESPHome -> dieses Geraet -> Zahnrad
-> "Diesem Geraet erlauben, Home Assistant Aktionen auszufuehren"*. Ohne das
schalten die Smarthome-Seiten nichts.

Die Konfigurationen enthalten **keine** Zugangsdaten; alles laeuft ueber
`!secret`.

### An dein Zuhause anpassen

Die Kern-Funktionen laufen ohne jede Aenderung: Menue + D-Pad-Bedienung,
Aktivitaeten, Bluetooth-HID (mehrere Hosts), beide Sprachassistenten,
Akku/Energie/Tiefschlaf, Statuszeile.

Die **Home-Assistant-Seiten im Menue** verweisen auf konkrete Entitaeten. Zwei
Stufen:

1. **Ueber `substitutions:`** (Block ganz oben in der YAML) — hier nur die IDs
   aendern: Media-Player/Streamer, Licht + Rollo Wohn-/Schlafzimmer,
   Kueche-Rollo, zwei Klingel-/Tuer-Paare, Sleeptimer.
2. **Fest in einzelnen Menue-Seiten** hinterlegt (kein `substitutions`):
   `page_room_ku` (Kueche), `page_room_bad` (Bad, inkl. Lüfter), `page_room_flur`
   (Flur), `page_all_rooms`, `page_music*` (Multiroom-Audio), Kamera- und
   Klingel-Seiten, einzelne Szenen und eine Steckdose. Diese IDs dort direkt
   ersetzen oder die Seite ignorieren — die Navigation bricht davon nicht,
   nicht belegte Buttons zeigen "nicht verfuegbar". (Die zunehmende Zahl neuer
   Funktionen macht eine vollstaendige `substitutions`-Abdeckung aller
   Entitaeten zu einem groesseren, noch offenen Umbau.)

| Datei / Ordner | Zweck |
|---|---|
| `open-remote.yaml` | Einbinde-Datei zum Adoptieren (Home Assistant) |
| `open-remote-web.yaml` | Variante fuer den Web-Installer (ohne Home Assistant) |
| `open-remote-firmware.yaml` | die Firmware |
| `components/` | die ESPHome-Komponenten |
| `fonts/` | mdi-subset (ein Ladesymbol; die Textschrift kommt per `gfonts://`) |
| `mikrofon-kontakttest.yaml` | eigenstaendiges Loetstellen-Pruefprogramm (siehe unten) |
| `docs/` | technische Notizen (ATVV-Angleichungen) |
| `secrets.yaml.example` | Vorlage fuer `secrets.yaml` |
| `sd-karte/` | Beispiel-Ordnerstruktur + fertige Cover fuer die optionale microSD-Karte ([Details](sd-karte/README.md)) |
| `make_sd_assets.py` | erzeugt die SD-Karten-Cover aus Simple-Icons (CC0) |
| `open-remote-pm.h`, `open-remote-musik.h`, `open-remote-strom.h`, `open-remote-eingabe.h`, `open-remote-sd-bilder.h` | von der Firmware eingebundene Helfer (Energie, Multiroom, Tasteneingabe, SD-Cover) |

## Stand

Infrarot ist auf der Platine vorhanden, wird von dieser Firmware aber noch nicht
genutzt — die Geraetesteuerung laeuft ueber Bluetooth-HID.

## Technische Notizen

Ein paar Punkte aus der Entwicklung, die auf andere Projekte uebertragbar sind:

- **I2S-Mikrofon stumm durch eine hochohmige Loetstelle** (1 V statt 3 V am
  Versorgungspad). Fuer solche Faelle: `mikrofon-kontakttest.yaml` (siehe unten).
- **Android TV baut bei jedem Verbinden die Oberflaeche laufender Apps neu auf**,
  sobald in der HID-Beschreibung eine Tastatur oder ein Zeigegeraet auftaucht.
  Deshalb meldet `espidf_ble_keyboard` nur Medientasten.
- **Ein Messwert, der zu einem Defekt passt, passt oft genauso gut zu einem
  gesunden Bauteil.** Beim Mikrofon-Test wird L/R deshalb erst dann bewertet,
  wenn die Datenleitungen als in Ordnung erkannt sind.
- **Ein stromloses I2S-Mikrofon bremst die SD-Karte aus, wenn sich beide Pins
  teilen**: Die ESD-Schutzdiode des abgeschalteten Mikrofons klemmt die
  gemeinsame MISO-Leitung nach Low, und der SD-Treiber wartet dadurch vor
  jedem Befehl bis zu 40 ms (`poll_busy`) — gemessen 5 KB/s statt 200+ KB/s,
  unabhaengig vom SPI-Takt. Loesung: die Versorgung des Mikrofons waehrend der
  Kartennutzung eingeschaltet lassen, nicht abschalten.
- **Ein Hardware-Bewegungsinterrupt ist nicht automatisch zuverlaessiger als
  Software-Polling.** Der LIS3DH-Interrupt fuer "Bewegung" loeste hier
  wiederholt aus, obwohl das Geraet unbewegt lag — nachweisbar per Log
  (Sensor-Auswertung meldete nichts, waehrend der Interrupt feuerte). Direktes
  Auslesen der Rohwerte mit Entprellung (mehrere Messungen in Folge) war am
  Ende zuverlaessiger als der eingebaute Interrupt-Pfad.
- **Ein Timer, der "zur Sicherheit" im Hintergrund weiterlaeuft, kann
  Zustaende zuruecksetzen, die man gerade bewusst haelt.** Ein "Menue nach X
  Sekunden vergessen"-Mechanismus lief auch dann weiter, wenn eine andere
  Seite (hier: der Begruessungsbildschirm) bewusst laenger stehen bleiben
  sollte — er ist an der Ursache oft schwerer zu finden als am Symptom, weil
  er nicht an der Stelle steht, die das Symptom zeigt.
- Details zum ATVV-Audiopfad: `docs/atv-voice-vs-reference-firmware.md`.

## Lizenz

MIT (siehe [`LICENSE`](LICENSE)) — frei nutzbar, aendern und weitergeben erlaubt.

Fremd-Assets und ihre Lizenzen: [`THIRD-PARTY.md`](THIRD-PARTY.md).

---

## Mikrofon-Kontakttest

`mikrofon-kontakttest.yaml` ist ein **eigenstaendiges Programm**: aufspielen,
Geraet startet neu, prueft sich selbst und meldet fuer jedes der sechs Pads des
MS3625, ob es angeloetet ist. Ergebnis als Text in Home Assistant und im Log.

```
Pad 1 GND             OK
Pad 2 VDD             OK
Pad 3 SD (Daten)      FEHLER - nicht angeloetet
Pad 4 SCK (Bit-Takt)  OK
Pad 5 WS (Wortwahl)   OK
Pad 6 L/R             nicht pruefbar - erst Leitungen loeten
Teststatus            fertig - Loetfehler gefunden
```

**Zwei Verfahren, weil nicht alle Pads gleich zugaenglich sind:**

*Elektrisch* — ein angeschlossener Chip klemmt seine Anschluesse ueber die
internen Schutzdioden auf die eigene Versorgung. Schaltet man MIC_VDD ab, zieht
das die Leitung auf LOW, auch gegen einen aktiven Pull-up. Bleibt sie HIGH,
haengt dort nichts. Damit pruefbar: SD, SCK, WS direkt, VDD und GND indirekt —
klemmt naemlich gar nichts, fehlt Versorgung oder Masse.

*Akustisch* — L/R haengt an Masse und an keinem Prozessor-Pin, ist elektrisch
also nicht pruefbar. Ohne festen L/R-Pegel sendet der MS3625 aber in **keinem**
Zeitfenster. Sechs Sekunden zuhoeren: kommt Ton, ist L/R in Ordnung.

**Der elektrische Teil laeuft bei `priority: 900`, also bevor das I2S die Pins
uebernimmt.** Spaeter gemessen zeigen SCK und WS nur den selbst getriebenen
Ruhepegel der Prozessor-Ausgaenge — funktionierende Leitungen saehen dann defekt
aus.

**Einschraenkungen:** Bei stillem Raum laesst sich Pad 6 nicht sicher von einem
Defekt unterscheiden; fuer die akustische Pruefung Umgebungsgeraeusch erzeugen.
Eine offene Datenleitung liefert Vollausschlag statt Stille — deshalb wird L/R
erst bewertet, wenn SD, SCK und WS in Ordnung sind.
