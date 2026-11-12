# What's new · Was ist neu

## 2026.11.12.2 (2026-11-12)

**Back to the previous firmware.** After an update the old firmware is still in the second partition of remote and dock. The workshop shows it (“Previous firmware on the device”) and offers “Back to previous” – no build, no upload. Plus a **Factory reset** button on remote and dock (settings, Bluetooth pairings, counters; firmware and Wi-Fi stay).

**Firmware channel stable/beta.** Settings → Devices & services → ESPHomeRemote → Configure: *beta* gets new versions a few days earlier (device files are switched to the beta branch; HACS shows betas only with “Show beta versions”). From now on releases go out as beta first and are then released for everyone.

**Complete backup.** Next to the normal backup, one file with the ESPHome device files, the remotes created in the configurator and, if you want, secrets.yaml. Restoring only adds what is missing – nothing is overwritten.

**Charge limit (experimental).** Battery page: charge a remote in the dock only up to 80/85/90 % – needs a dock with the charging-contact retrofit (GPIO2); Home Assistant switches the contacts off at the limit and on again 5 % below, after 2 hours or when the remote is taken out.

**Also:** the remote's flash write interval is 5 min instead of 1 min (less wear; it writes before deep sleep and restarts anyway). Configurator: the styles now live in styles.js (1,400 lines less in configurator.js, no visible change). GitHub Discussions are open for questions, ideas and photos of your setup. The firmware update is optional – it only brings the rollback button, factory reset and the write interval.

<details><summary>Deutsch</summary>

**Zurück zur vorherigen Firmware.** Nach einem Update liegt der alte Stand noch in der zweiten Partition von Remote und Dock. Die Werkstatt zeigt ihn („Vorherige Firmware auf dem Gerät“) und bietet „Zurück zur vorherigen“ – ohne Bauen, ohne Hochladen. Dazu der Knopf **Werkseinstellungen** an Remote und Dock (Einstellungen, Bluetooth-Kopplungen, Zähler; Firmware und WLAN bleiben).

**Firmware-Kanal stabil/beta.** Einstellungen → Geräte & Dienste → ESPHomeRemote → Konfigurieren: *beta* bekommt neue Versionen ein paar Tage früher (Gerätedateien werden auf den Beta-Zweig umgestellt, HACS zeigt Betas nur mit „Beta-Versionen anzeigen“). Releases gehen künftig erst als Beta raus und werden dann für alle freigegeben.

**Komplett-Sicherung.** Neben der normalen Sicherung eine Datei mit den ESPHome-Gerätedateien, den im Konfigurator angelegten Remotes und auf Wunsch der secrets.yaml. Beim Einspielen wird nur ergänzt, was fehlt – nichts überschrieben.

**Ladelimit (experimentell).** Seite Akku: Remote im Dock nur bis 80/85/90 % laden – braucht ein Dock mit der Ladekontakt-Nachrüstung (GPIO2); Home Assistant schaltet die Kontakte ab dem Limit aus und 5 % darunter, nach 2 Stunden oder bei Entnahme wieder an.

**Außerdem:** Flash-Schreibintervall der Remote 5 min statt 1 min (weniger Verschleiß; vor Tiefschlaf und Neustart wird ohnehin geschrieben). Konfigurator: die Stile liegen jetzt in styles.js (1.400 Zeilen weniger in configurator.js, keine Änderung an der Oberfläche). GitHub Discussions sind offen für Fragen, Ideen und Fotos eurer Einrichtung. Firmware-Update ist freiwillig – es bringt nur Rollback-Knopf, Werkseinstellungen und das Schreibintervall.

</details>

## 2026.11.12.1 (2026-11-12)

**For bug reports: diagnostics file.** Settings → Devices & services → ESPHomeRemote → ⋮ → Download diagnostics: versions, firmware state per device, connection path, pending settings, last build errors – without names, addresses, keys or key layouts.

**Repairs in Home Assistant:** old ESPHomeRemote files in the ESPHome folder (with a Fix button), remotes that still need one USB flash (firmware before 2026.11.5.4), and Home Assistant without Supervisor now show up under Settings → Repairs.

**Home Assistant Container/Core:** when added, the integration says that it cannot build or flash without the ESPHome add-on and runs anyway; the ESPHome folder comes from the HA configuration instead of a fixed /config/esphome.

**For contributors:** integration tests (start without add-on, diagnostics, backup ⇄ restore, repairs, IR encoder, language files) run on every push on GitHub; CONTRIBUTING.md explains the layout, running without hardware and pull requests; Dependabot keeps the actions current; if the weekly test build fails with the newest ESPHome, GitHub opens an issue. Firmware unchanged – the offered update is optional.

<details><summary>Deutsch</summary>

**Für Fehlerberichte: Diagnose-Datei.** Einstellungen → Geräte & Dienste → ESPHomeRemote → ⋮ → Diagnose herunterladen: Versionen, Firmware-Stand je Gerät, Verbindungsweg, offene Einstellungen, letzte Bau-Fehler – ohne Namen, Adressen, Schlüssel oder Tastenbelegung.

**Reparaturen in Home Assistant:** alte ESPHomeRemote-Dateien im ESPHome-Ordner (mit Beheben-Knopf), Remotes, die noch einmal per USB müssen (Firmware vor 2026.11.5.4), und Home Assistant ohne Supervisor erscheinen jetzt unter Einstellungen → Reparaturen.

**Home Assistant Container/Core:** die Integration sagt beim Hinzufügen, dass sie ohne ESPHome-Add-on nicht bauen und flashen kann, und läuft trotzdem; der ESPHome-Ordner kommt aus der HA-Konfiguration statt fest /config/esphome.

**Für Mitentwickler:** Tests der Integration (Start ohne Add-on, Diagnose, Sicherung ⇄ Einspielen, Reparaturen, IR-Kodierer, Sprachdateien) laufen bei jedem Push auf GitHub; CONTRIBUTING.md erklärt Aufbau, Testlauf ohne Hardware und Pull Requests; Dependabot hält die Actions aktuell; scheitert der wöchentliche Testbau mit der neuesten ESPHome-Version, legt GitHub ein Issue an. Firmware unverändert – das angebotene Update ist freiwillig.

</details>

## 2026.11.10.7 (2026-11-10)

Configurator on phones: Bluetooth page (notice and selects), IR codes (remote select) and notice boxes on other pages no longer squeezed; 'waiting for the remote' rows are one line on phones. English: song titles and calendar entries are no longer partly translated, missing tooltips added. New guide: external and extra IR LEDs on the dock (docs/dock-ir-led.md).

<details><summary>Deutsch</summary>

Konfigurator am Handy: Bluetooth-Seite (Hinweis und Auswahlfelder), IR-Codes (Remote-Auswahl) und Hinweiskästen in anderen Seiten nicht mehr gequetscht; Zeilen „wartet auf die Remote“ am Handy einzeilig. Englisch: Liedtitel und Kalendereinträge werden nicht mehr teilübersetzt, fehlende Tooltips ergänzt. Neue Anleitung: externe und zusätzliche IR-LEDs am Dock (docs/dock-ir-led.md).

</details>

## 2026.11.10.6 (2026-11-10)

Configurator: after an update, browsers and the phone app sometimes kept showing the old version (e.g. 5 notices instead of collapsed) because the panel's version tag wasn't bumped. It is now derived from the content – every update loads the new version. Firmware unchanged – the offered update is optional.

<details><summary>Deutsch</summary>

Konfigurator: Nach einem Update zeigten Browser und Handy-App teils weiter die alte Fassung (z. B. 5 Hinweise statt eingeklappt), weil die Versionskennung des Panels nicht mitgezählt wurde. Jetzt ergibt sie sich aus dem Inhalt – nach jedem Update lädt die neue Fassung. Firmware unverändert – das angebotene Update ist freiwillig.

</details>

## 2026.11.10.5 (2026-11-10)

Configurator: notices on the overview are collapsed – the header shows how many and how important, one click expands them. “First steps”: “Pair via Bluetooth” was already ticked on a fresh install (default slot names counted as paired). English UI: about 40 remaining German texts translated (streamer guide, features, confirmations when deleting or transferring). Fallback font now also covers → ↑ ↓. Firmware unchanged – the offered update is optional.

<details><summary>Deutsch</summary>

Konfigurator: Hinweise in der Übersicht sind eingeklappt – der Kopf zeigt Anzahl und Wichtigkeit, ein Klick klappt auf. „Erste Schritte“: „Bluetooth koppeln“ war bei einer frischen Installation schon abgehakt (Standard-Platznamen zählten als gekoppelt). Englische Oberfläche: rund 40 noch deutsche Texte übersetzt (Streamer-Anleitung, Funktionen, Rückfragen beim Löschen und Übertragen). Ersatzschrift enthält jetzt auch → ↑ ↓. Firmware unverändert – das angebotene Update ist freiwillig.

</details>

## 2026.11.10.4 (2026-11-10)

**No more inventing secrets.** When you add the integration, it now writes the passwords and keys the remote needs into the ESPHome secrets for you (tick on by default; existing lines are never changed). An `esphomeremote_key` that is already there is taken over, so remote and integration always match. Only Wi-Fi you enter yourself.

**Configurator, tidier:**
- **Keys page:** the activity settings fold into one line – the remote and the key editor are visible right away.
- **Status bar:** shows only the remote you are working on; "waiting for the remote" and pending settings share one line. What is running or failing still shows everywhere.
- **Overview:** the most important notices first (5 shown, the rest on request), a notice when a remote hasn't reported, "last at dock …" on each card, battery chart with a 50 % line, charging marks and values per hour on hover.
- **First steps:** an optional card for new users (pair Bluetooth, assign a key, home screen, dock) – ticks itself off and can be hidden.
- **Share activities:** *Share* copies an activity as text, *Import* adds one from someone else.
- **Symbols:** characters like ⇄ ⏯ ⏮ ⏭ now show in every theme, even if its font lacks them (small built-in fallback font).

Remotes and docks: no firmware change.

<details><summary>Deutsch</summary>

**Keine Secrets mehr ausdenken.** Beim Hinzufügen der Integration trägt sie die Passwörter und Schlüssel, die die Remote braucht, selbst in die ESPHome-Secrets ein (Haken ist an; vorhandene Zeilen bleiben unverändert). Ein schon vorhandener `esphomeremote_key` wird übernommen, damit Remote und Integration immer zusammenpassen. Nur das WLAN trägst du selbst ein.

**Konfigurator aufgeräumt:**
- **Tasten:** Die Einstellungen der Aktivität klappen zu einer Zeile zusammen – Remote und Tasten-Editor sind sofort zu sehen.
- **Statusleiste:** zeigt nur die Remote, an der du gerade arbeitest; „wartet auf die Remote“ und offene Einstellungen in einer Zeile. Was läuft oder scheitert, steht weiter überall.
- **Übersicht:** Wichtiges zuerst (5 Hinweise, der Rest auf Wunsch), ein Hinweis, wenn sich eine Remote nicht meldet, „zuletzt bei Dock …“ auf jeder Karte, Akku-Kurve mit 50-%-Linie, Lade-Markierung und Stundenwerten beim Darüberfahren.
- **Erste Schritte:** eine freiwillige Karte für Neue (Bluetooth koppeln, Taste belegen, Startseite, Dock) – hakt sich selbst ab und lässt sich ausblenden.
- **Aktivitäten teilen:** *Teilen* kopiert eine Aktivität als Text, *Importieren* fügt eine von jemand anderem ein.
- **Symbole:** Zeichen wie ⇄ ⏯ ⏮ ⏭ erscheinen jetzt in jedem Design, auch wenn dessen Schrift sie nicht hat (kleine eingebaute Ersatzschrift).

Remotes und Docks: keine neue Firmware.

</details>

## 2026.11.10.3 (2026-11-10)

**Warnings in Home Assistant.** Under *Settings → Repairs* you now get a notice when a remote's battery drops below 15 % (unless it is charging; it disappears by itself above 20 %) and when a remote hasn't reported for 24 hours. When it last reported is kept across restarts. Notices can be ignored as usual.

**Between two repeaters.** The remote now also learns the dock's radio channel while its Wi-Fi is on. Before, if the dock's access point changed its channel (e.g. automatic channel selection), the remote kept taking the slower home-network route, and with Wi-Fi off the first key press tried the old channel first.

**Every release is test-built.** GitHub now builds the remote and dock firmware from `esphomeremote.yaml` and `esphomeremote-dock.yaml` on every change – exactly what you build at home.

Update over Wi-Fi as usual: docks first, then the remotes.

<details><summary>Deutsch</summary>

**Warnungen in Home Assistant.** Unter *Einstellungen → Reparaturen* gibt es jetzt einen Hinweis, wenn der Akku einer Remote unter 15 % fällt (außer sie lädt; er verschwindet von selbst über 20 %) und wenn sich eine Remote 24 Stunden nicht gemeldet hat. Wann sie sich zuletzt gemeldet hat, bleibt über Neustarts erhalten. Hinweise lassen sich wie gewohnt ignorieren.

**Zwischen zwei Repeatern.** Die Remote lernt den Funkkanal des Docks jetzt auch bei eingeschaltetem WLAN. Wechselte der Zugangspunkt des Docks seinen Kanal (z. B. automatische Kanalwahl), nahm sie vorher dauerhaft den langsameren Weg übers Heimnetz, und mit WLAN aus probierte der erste Tastendruck zuerst den alten Kanal.

**Jedes Release wird probegebaut.** GitHub baut jetzt bei jeder Änderung die Firmware für Remote und Dock aus `esphomeremote.yaml` und `esphomeremote-dock.yaml` – genau das, was du zu Hause baust.

Update wie gewohnt über WLAN: erst die Docks, dann die Remotes.

</details>

## 2026.11.10.2 (2026-11-10)

**Easier setup.** The setup dialog now shows a complete, ready-to-copy block for the ESPHome secrets – passwords and the radio key for the dock are generated for you. No more inventing passwords or running `openssl`.

**New guide.** Step-by-step quick start with one-click buttons for HACS and the integration, a troubleshooting table for the most common setup problems, and issue forms that ask for everything needed to help.

**Dock values survive a restart.** Battery, charging cable, activity and the other values reported via the dock keep their last state after a Home Assistant restart (attribute `wiederhergestellt: true` until the dock reports again) instead of showing "unavailable".

The firmware itself is unchanged. The configurator still offers the firmware update to this version – optional, nothing changes on the remote or dock.

<details><summary>Deutsch</summary>

**Einfachere Einrichtung.** Der Einrichtungsdialog zeigt jetzt einen kompletten Block zum Kopieren für die ESPHome-Secrets – Passwörter und Funkschlüssel fürs Dock sind schon erzeugt. Kein Ausdenken von Passwörtern und kein `openssl` mehr.

**Neue Anleitung.** Schnellstart Schritt für Schritt mit Knöpfen für HACS und die Integration, eine Tabelle für die häufigsten Probleme bei der Einrichtung und Issue-Formulare, die alles Nötige abfragen.

**Dock-Werte überstehen einen Neustart.** Akku, Ladekabel, Aktivität und die übrigen Werte über das Dock behalten nach einem Neustart von Home Assistant ihren letzten Stand (Attribut `wiederhergestellt: true`, bis das Dock wieder meldet) statt „nicht verfügbar“.

Die Firmware selbst ist unverändert. Der Konfigurator bietet das Firmware-Update auf diese Version trotzdem an – freiwillig, an Remote und Dock ändert sich nichts.

</details>

## 2026.11.10.1 (2026-11-10)

Fixes, mostly for fresh installs from HACS.

**New remotes work right away.** The configurator key mapping is now on by default. Before, a newly added remote had no activities, the keys did nothing and only the i key woke the display. If it is switched off, the configurator now warns clearly.

**Announcements over Wi-Fi.** The simple firmware from GitHub now sends announcements straight to Home Assistant over Wi-Fi – before it always said "Not possible right now".

**Display no longer goes black when dimmed.** Since the switch to 20 kHz, low brightness levels (3–5 %) rounded down to zero.

**Waking up.** The wake-up table no longer sends real infrared or key presses to the TV on every restart/OTA. It is only used for infrared "dock only"; with "remote and dock" the remote's own infrared was missing on wake-up. If the dock reports "no Home Assistant", the *Without Home Assistant* mapping applies immediately instead of after 15 s.

**Battery and status via the dock** also work with the default device name `esphomeremote` (before only for names starting with `remote-`).

**Home Assistant address.** With an https internal URL (reverse proxy, own domain) or none, the remote uses HA's local IP and port instead of hanging on `homeassistant.local`.

**Voice assistant** finds the remote's pipeline selection in English Home Assistant installations too.

**Streamer and TV menu.** "Hold menu key = camera" finds the TV with the default "first with ADB"; the audio switch picks up a changed state source; the TV menu rebuilds after saving (before, tiles could run the wrong entry); the safety prompt works; options the TV menu does not support are no longer offered; leaving the editor no longer loses unsaved changes.

**Backup.** Announcement and TV menu settings are now restored from a backup (they were skipped silently).

**Configurator.** The announcement settings no longer clash with the dock search speed; the firmware page of remotes and docks shows jobs and the log again; cancelling a job stops the remote's update Wi-Fi; the developer switch and the empty hardware test are hidden in the HACS version; debug logging is off for new remotes.

Update over Wi-Fi as usual: docks first, then the remotes.

<details><summary>Deutsch</summary>

Korrekturen, vor allem für Neuinstallationen über HACS.

**Neue Remotes gehen sofort.** Die Konfigurator-Belegung ist jetzt von Anfang an an. Vorher hatte eine neu hinzugefügte Remote keine Aktivitäten, die Tasten taten nichts, nur die i-Taste weckte das Display. Ist sie aus, warnt der Konfigurator jetzt deutlich.

**Durchsage per WLAN.** Die einfache Firmware von GitHub schickt Durchsagen jetzt per WLAN direkt an Home Assistant – vorher kam immer „Geht gerade nicht“.

**Display wird beim Abdunkeln nicht mehr schwarz.** Seit 20 kHz rundeten niedrige Helligkeiten (3–5 %) auf null.

**Aufwachen.** Die Weck-Tabelle sendet bei Neustart/OTA kein echtes Infrarot und keine Tasten mehr an den Fernseher. Sie gilt nur noch für Infrarot „nur Dock“; bei „Remote und Dock“ fehlte beim Wecken das eigene Infrarot der Remote. Meldet das Dock „kein Home Assistant“, gilt *Ohne Home Assistant* sofort statt nach 15 s.

**Akku und Status über das Dock** gehen auch mit dem Standardnamen `esphomeremote` (vorher nur für Namen mit `remote-`).

**Adresse von Home Assistant.** Bei interner URL mit https (Reverse-Proxy, eigene Domain) oder ohne nimmt die Remote HAs lokale IP und Port, statt bei `homeassistant.local` hängen zu bleiben.

**Sprachassistent** findet die Pipeline-Auswahl der Remote auch in englischen Home-Assistant-Installationen.

**Streamer und Menü am Fernseher.** „Menü-Taste halten = Kamera“ findet den Fernseher mit dem Standard „erster mit ADB“; der Audio-Schalter übernimmt eine geänderte Zustandsquelle; das Menü am Fernseher baut sich nach dem Speichern neu auf (vorher konnten Kacheln den falschen Eintrag ausführen); die Sicherheitsabfrage wirkt; nicht unterstützte Optionen werden dort nicht mehr angeboten; beim Verlassen des Editors gehen ungespeicherte Änderungen nicht mehr verloren.

**Sicherung.** Durchsage und Menü am Fernseher werden beim Einspielen jetzt mit wiederhergestellt (wurden still übergangen).

**Konfigurator.** Die Durchsage-Einstellungen kommen dem Suchtempo der Docks nicht mehr in die Quere; die Firmware-Seite von Remotes und Docks zeigt wieder Aufträge und Protokoll; ein Abbruch schaltet das Update-WLAN der Remote aus; Entwickler-Schalter und leerer Hardware-Test sind in der HACS-Fassung ausgeblendet; Debug-Protokoll ist für neue Remotes aus.

Update wie gewohnt über WLAN: erst die Docks, dann die Remotes.

</details>

## 2026.11.9.1 (2026-11-09)

**Clearer voice.** The display and key backlights were dimmed at 1 kHz, which whistled into the microphone (1 to 7 kHz). Now 20 kHz: the whine is gone – for voice search on the TV, the voice assistant and announcements.

**Clock via the dock.** In dock mode the remote gets the time from the dock – even after a cold start without Wi-Fi.

**Dock without Home Assistant.** If the dock has no connection to HA, the remote uses the *Without Home Assistant* key mapping. If HA is reachable through the dock, that counts as connected (before, e.g. the volume fell back to infrared after 15 s instead of using the speaker in HA).

**Wakes up faster.** The display appears noticeably sooner after waking, the backlight no longer flashes briefly, and pressing the wake key again during start-up no longer counts as a double press.

**Streamer.** Holding the menu key is configurable (e.g. show/hide a camera); `esphomeremote.camera_tv` takes `toggle` (if the camera is already showing, it is hidden).

**Firmware workshop.** Running jobs can be cancelled – the build in the ESPHome Device Builder is stopped too.

Update over Wi-Fi as usual: docks first, then the remotes.

<details><summary>Deutsch</summary>

**Klarere Sprache.** Display- und Tastenbeleuchtung wurden mit 1 kHz gedimmt – das pfiff ins Mikrofon (1 bis 7 kHz). Jetzt 20 kHz: das Pfeifen ist weg, bei der Sprachsuche am Fernseher, beim Sprachassistenten und bei Durchsagen.

**Uhrzeit über das Dock.** Im Dock-Betrieb holt sich die Remote die Uhrzeit vom Dock – auch nach einem Kaltstart ohne WLAN.

**Dock ohne Home Assistant.** Hat das Dock keine Verbindung zu HA, nimmt die Remote die Belegung *Ohne Home Assistant*. Ist HA über das Dock erreichbar, gilt das als verbunden (vorher ging z. B. die Lautstärke nach 15 s per Infrarot statt über den Lautsprecher in HA).

**Schneller wach.** Das Display ist beim Aufwachen spürbar früher da, die Beleuchtung blitzt nicht mehr kurz auf, und nochmaliges Drücken der Weck-Taste während des Starts zählt nicht mehr als Doppeldruck.

**Streamer.** Menü-Taste gedrückt halten ist einstellbar (z. B. Kamera zeigen/ausblenden); `esphomeremote.camera_tv` kennt `toggle` (läuft die Kamera schon, wird sie ausgeblendet).

**Firmware-Werkstatt.** Laufende Aufträge lassen sich abbrechen – der Bau im ESPHome Device Builder wird mit beendet.

Update wie gewohnt über WLAN: erst die Docks, dann die Remotes.

</details>

## 2026.11.6.1 (2026-11-06)

**Announcements.** New remote function *Announcement while held* (long press only): hold the key, speak, release – Home Assistant plays it on a speaker of your choice, e.g. a Chromecast group, with a chime and the volume raised. Settings in the configurator under *Functions → Announcement*: on/off, speaker, volume for the announcement (restored afterwards), chime. Up to about 25 s. The recording is cleaned up: interference from the remote's electronics (whine at 1 kHz and its multiples) is filtered out, and the clicks when the microphone starts and when you release the key are cut off.

**Voice assistant via the dock.** In dock mode the remote now streams speech to Home Assistant through the dock (or the Bluetooth link) while you speak – Wi-Fi stays off. With Wi-Fi on everything works as before.

**Firmware workshop.** Remotes and docks share one queue: reorder jobs (up/down, first/last), remove waiting ones, and every job shows what it does (scheduled, build only, USB, Wi-Fi). The log below always belongs to the job you picked. Progress output (upload 1 % … 100 %) now overwrites its line like in a terminal instead of adding a new line for every step.

**Tidier settings.** The explanations on the *Settings* page are collapsed behind an ⓘ next to each setting and card title; **Show all explanations** opens them all (remembered in the browser).

Update over Wi-Fi as usual – no USB needed this time.

<details><summary>Deutsch</summary>

**Durchsage.** Neue Funktion der Remote *Durchsage, solange gedrückt* (nur für Lang): Taste halten, sprechen, loslassen – Home Assistant spielt es auf einem Lautsprecher deiner Wahl ab, z. B. einer Chromecast-Gruppe, mit Gong und angehobener Lautstärke. Einstellungen im Konfigurator unter *Funktionen → Durchsage*: an/aus, Lautsprecher, Lautstärke für die Durchsage (danach zurückgestellt), Gong. Bis etwa 25 s. Die Aufnahme wird gesäubert: Störungen der Remote-Elektronik (Pfeifen bei 1 kHz und Vielfachen) werden herausgefiltert, das Knacken beim Start des Mikrofons und beim Loslassen der Taste abgeschnitten.

**Sprachassistent übers Dock.** Im Dock-Betrieb schickt die Remote die Sprache jetzt schon während des Sprechens über das Dock (oder die Bluetooth-Verbindung) an Home Assistant – das WLAN bleibt aus. Mit WLAN bleibt alles wie bisher.

**Firmware-Werkstatt.** Remotes und Docks haben eine gemeinsame Warteschlange: Aufträge verschieben (hoch/runter, ganz nach oben/unten), wartende entfernen, und jeder Auftrag zeigt, was er tut (vorgemerkt, nur bauen, USB, WLAN). Das Log darunter gehört immer zum gewählten Auftrag. Fortschrittsanzeigen (Upload 1 % … 100 %) überschreiben ihre Zeile jetzt wie im Terminal, statt für jede Stufe eine neue Zeile anzuhängen.

**Aufgeräumte Einstellungen.** Die Erklärungen auf der Seite *Einstellungen* sind hinter einem ⓘ an jeder Einstellung und Kartenüberschrift eingeklappt; **Alle Erklärungen zeigen** klappt alle auf (merkt sich der Browser).

Update wie gewohnt über WLAN – diesmal ohne USB.

</details>

## 2026.11.5.4 (2026-11-05)

> ⚠️ **Flash each remote once via USB cable.** The faster wake-up needs a new bootloader, and a bootloader can't be updated over Wi-Fi. In the configurator: *Firmware* page of the remote → **USB** (Chrome or Edge on a PC). Update the **docks first** (over Wi-Fi as usual). Wi-Fi updates of the remote keep working without USB – it just wakes about 0.5 s slower.

**Wakes up in a blink.** In dock mode, a key press now reaches the dock about **0.2 s** after waking (before: about 3 s). The key is read and sent straight after power-up, before the display and the rest of the firmware start; the remote loads a short version of its configuration first; the PSRAM self-test at boot is gone. Also works when the remote is woken by motion and the key comes a moment later.

**Menu on the TV.** Every streamer with the Streamer app (1.19) can show a popup menu on the TV – built with the same editor as the remote's menu (pages, tiles, lights, scenes, covers, cameras, volume, door openers with a yes/no prompt). In the configurator at the streamer: *Menu on the TV* – menu key (learn it with the streamer's remote), size from 30 % to full screen, nine positions, display time, **Edit menu**, **Show on the TV**. Operated with the arrows, OK and Back of the streamer's remote; on TVs that forward their remote via HDMI-CEC the TV remote works too. New action `esphomeremote.streamer_menu` (`command: toggle/open/close`, optional `page`, `size`, `position` for that one opening) – e.g. for a key on the remote.

**Soundbar via HDMI-CEC.** Per streamer, *Soundbar / AV receiver via HDMI* turns the audio device on the TV's HDMI-ARC on and off through the streamer – no infrared. You get `switch.esphomeremote_streamer_<id>_soundbar` (state optionally from an entity, e.g. the TV's audio output) and the action `esphomeremote.streamer_cec` (`device: soundbar/tv`, `command: on/off`).

**Camera on the TV, per call.** `esphomeremote.camera_tv` takes optional `size`, `position` and `seconds` (display time, 0 = until off) for that one call; the configurator settings stay as they are.

**Streamer keys for automations.** Keys of the streamer's remote can be reported to Home Assistant as the event `esphomeremote_streamer_key` (`code`, `name`, `press: short/long/learn`, `source`, `cec`).

**Fixes:** the soundbar switch no longer updates its state from a worker thread; the TV menu no longer flickers or jumps while it refreshes, and arrow keys move to the nearest tile in the next row.

<details><summary>Deutsch</summary>

> ⚠️ **Jede Remote einmal per USB-Kabel aufspielen.** Das schnellere Aufwachen braucht einen neuen Bootloader, und ein Bootloader lässt sich nicht über WLAN aktualisieren. Im Konfigurator: Seite *Firmware* der Remote → **USB** (Chrome oder Edge am PC). **Docks zuerst** aktualisieren (wie gewohnt über WLAN). WLAN-Updates der Remote gehen auch ohne USB – sie wacht dann nur etwa 0,5 s langsamer auf.

**Wacht im Nu auf.** Im Dock-Betrieb ist ein Tastendruck jetzt etwa **0,2 s** nach dem Aufwachen beim Dock (vorher etwa 3 s). Die Taste wird direkt nach dem Einschalten gelesen und gesendet, noch bevor Display und restliche Firmware starten; die Remote lädt zuerst eine kurze Fassung ihrer Konfiguration; der PSRAM-Selbsttest beim Start entfällt. Klappt auch, wenn die Remote durch Bewegung aufwacht und die Taste kurz danach kommt.

**Menü auf dem Fernseher.** Jeder Streamer mit der Streamer-App (1.19) kann ein Popup-Menü auf dem Fernseher zeigen – gebaut mit demselben Editor wie das Menü der Remote (Seiten, Kacheln, Licht, Szenen, Rollos, Kameras, Lautstärke, Türöffner mit Ja/Nein-Abfrage). Im Konfigurator beim Streamer: *Menü auf dem Fernseher* – Menü-Taste (mit der Streamer-Fernbedienung anlernen), Größe von 30 % bis ganzer Bildschirm, neun Lagen, Einblenddauer, **Menü bearbeiten**, **Auf dem Fernseher zeigen**. Bedient mit Pfeilen, OK und Zurück der Streamer-Fernbedienung; bei Fernsehern, die ihre Fernbedienung per HDMI-CEC weiterreichen, auch mit der des Fernsehers. Neue Aktion `esphomeremote.streamer_menu` (`command: toggle/open/close`, optional `page`, `size`, `position` nur für dieses Öffnen) – z. B. für eine Taste der Remote.

**Soundbar per HDMI-CEC.** Je Streamer schaltet *Soundbar / AV-Receiver per HDMI* das Audiogerät am HDMI-ARC des Fernsehers über den Streamer ein und aus – ohne Infrarot. Dazu gibt es `switch.esphomeremote_streamer_<id>_soundbar` (Zustand wahlweise aus einer Entität, z. B. der Tonausgabe des Fernsehers) und die Aktion `esphomeremote.streamer_cec` (`device: soundbar/tv`, `command: on/off`).

**Kamera auf dem Fernseher je Aufruf.** `esphomeremote.camera_tv` nimmt optional `size`, `position` und `seconds` (Einblenddauer, 0 = bis off) nur für diesen Aufruf; die Einstellungen im Konfigurator bleiben.

**Streamer-Tasten für Automationen.** Tasten der Streamer-Fernbedienung lassen sich als Ereignis `esphomeremote_streamer_key` an Home Assistant melden (`code`, `name`, `press: short/long/learn`, `source`, `cec`).

**Behoben:** Der Soundbar-Schalter schreibt seinen Zustand nicht mehr aus einem Hintergrund-Thread; das Menü auf dem Fernseher flackert und springt beim Aktualisieren nicht mehr, und die Pfeiltasten gehen zur nächsten Kachel in der nächsten Reihe.

</details>

## 2026.11.5.3 (2026-11-05)

- **Cleanup:** the transition layer from the rename (esphomeremote_konfig → esphomeremote) is removed: old service names `esphomeremote_konfig.*`, the old `/api/esphomeremote_konfig/…` paths and the duplicate `sensor.esphomeremote_konfig_*` sensors (orphaned entries are removed at startup).
- **Before updating:** update remotes and docks running firmware from before 2 Nov 2026 first – they still used the old names.

<details><summary>Deutsch</summary>

- **Aufgeräumt:** Die Übergangsschicht aus der Umbenennung (esphomeremote_konfig → esphomeremote) ist entfernt: alte Dienstnamen `esphomeremote_konfig.*`, die alten Adressen `/api/esphomeremote_konfig/…` und die doppelten Sensoren `sensor.esphomeremote_konfig_*` (verwaiste Einträge werden beim Start entfernt).
- **Vorher:** Remotes und Docks mit Firmware von vor dem 2. 11. 2026 erst aktualisieren – sie riefen noch die alten Namen auf.

</details>

## 2026.11.5.2 (2026-11-05)

**Streamer app knows "camera only".** When "camera only" is on for a streamer, Home Assistant now tells the app too: it no longer subscribes to window events (Android delivers nothing to it) and only reacts to camera commands. The app's status page shows a green "camera only" instead of red hints about missing notification access. To apply, tap "Update" at the streamer in the configurator.

**No more error at startup.** The old service names esphomeremote_konfig.* (for devices with older firmware) now have a description – Home Assistant no longer logs "Failed to load services.yaml" on every start.

<details><summary>Deutsch</summary>

**Streamer-App kennt „Nur Kamera“.** Ist beim Streamer „Nur Kamera“ an, sagt Home Assistant das jetzt auch der App: sie abonniert dann keine Fenster-Ereignisse mehr (Android stellt ihr gar nichts zu) und reagiert nur noch auf Kamera-Befehle. Die Statusseite der App zeigt grün „Nur Kamera“ statt roter Hinweise auf fehlenden Benachrichtigungszugriff. Zum Übernehmen im Konfigurator beim Streamer auf „Aktualisieren“ tippen.

**Keine Fehlermeldung mehr beim Start.** Die alten Dienstnamen esphomeremote_konfig.* (für Geräte mit älterer Firmware) haben jetzt eine Beschreibung – Home Assistant meldet nicht mehr bei jedem Start „Failed to load services.yaml“.

</details>

## 2026.11.5.1 (2026-11-05)

**Clean removal.** Deleting the integration entry now removes ESPHomeRemote without leftovers: the configurator disappears from the sidebar right away (no restart), settings, IR database and images are deleted, the streamer app is uninstalled from the streamers, services, repair issues and orphaned entities are removed. The ESPHome files of remotes and docks stay (they belong to the ESPHome add-on). If you already uninstalled and still see the configurator in the sidebar: install this version, delete the entry, then remove it in HACS.

**Camera on several TVs.** Under Functions › Camera on the TV you can add more TVs, each with a free name, its own streamer, camera list, image source, size and corner. The actions camera_tv, camera_size and camera_position have a tv field for this; without tv the first TV is used as before. Each TV gets its own entities (binary_sensor.esphomeremote_camera_tv_<name>).

**Step through cameras.** camera_tv accepts camera: next and previous – through the TV's camera list.

**Substream image source.** Per TV: image straight from the go2rtc substream (<camera>_sub). Needed for streamers that cannot decode high resolutions (e.g. MagentaTV Stick: otherwise only the play icon).

**MagentaTV Stick as streamer.** Device type per streamer (Google TV Streamer, MagentaTV Stick Gen 2, other Android TV) with matching setup guide. Streamers on Android 12 are now recognised correctly (title and state did not arrive there). MagentaTV reports the programme as title and the channel as artist; live TV no longer reports a bogus position.

**Camera only.** Switch per streamer: the app only shows the camera popup, reports nothing to Home Assistant and is no longer polled – less load for weak sticks.

<details><summary>Deutsch</summary>

**Vollständig entfernen.** Wird der Integrationseintrag gelöscht, räumt ESPHomeRemote jetzt rückstandslos auf: der Konfigurator verschwindet sofort aus der Seitenleiste (ohne Neustart), Einstellungen, IR-Datenbank und Bilder werden gelöscht, die Streamer-App wird von den Streamern deinstalliert, Dienste, Reparatur-Hinweise und verwaiste Entitäten verschwinden. Die ESPHome-Dateien der Remotes und Docks bleiben (sie gehören zum ESPHome-Add-on). Wer die Integration schon deinstalliert hat und den Konfigurator noch in der Seitenleiste sieht: Version installieren, Eintrag löschen, dann in HACS entfernen.

**Kamera auf mehreren Fernsehern.** Unter Funktionen › Kamera auf dem Fernseher lassen sich weitere Fernseher anlegen, jeder mit frei wählbarem Namen, eigenem Streamer, eigener Kameraliste, Bildquelle, Größe und Ecke. Aktionen camera_tv, camera_size und camera_position haben dafür das Feld tv; ohne tv gilt wie bisher der erste Fernseher. Pro Fernseher gibt es eigene Entitäten (binary_sensor.esphomeremote_camera_tv_<name>).

**Kameras weiterschalten.** camera_tv kennt camera: next und previous (auch nächste/vorherige) – durch die Kameraliste des Fernsehers.

**Bildquelle Substream.** Pro Fernseher wählbar: Bild direkt vom go2rtc-Substream (<kamera>_sub). Nötig für Streamer, die hohe Auflösungen nicht dekodieren (z. B. MagentaTV Stick: sonst nur das Play-Symbol).

**MagentaTV Stick als Streamer.** Gerätetyp pro Streamer (Google TV Streamer, MagentaTV Stick Gen 2, anderes Android-TV) mit passender Einrichtungsanleitung. Streamer mit Android 12 werden jetzt richtig erkannt (Titel und Status kamen dort nicht an). MagentaTV meldet Sendung als Titel und Sender als Interpret; Live-TV meldet keine unsinnige Position mehr.

**Nur Kamera.** Schalter pro Streamer: die App zeigt nur noch das Kamera-Popup, meldet nichts an Home Assistant und wird nicht mehr laufend abgefragt – weniger Last für schwache Sticks.

</details>

## 2026.11.4.7 (2026-11-04)

**README readable in HACS.** HACS shows the README of the release, could not load relative images and printed the logo's <picture> block as text. The logo is a plain image now (on a dark card, readable in light and dark mode), and all images and links use absolute URLs. No functional changes; the firmware is unchanged.

<details><summary>Deutsch</summary>

**README in HACS lesbar.** HACS zeigt das README des Releases, konnte relative Bilder nicht laden und hat den <picture>-Block des Logos als Text ausgegeben. Das Logo ist jetzt ein normales Bild (auf dunkler Karte, in hellem und dunklem Design lesbar), alle Bilder und Links haben feste Adressen. Keine Funktionsänderung, die Firmware ist unverändert.

</details>

## 2026.11.4.6 (2026-11-04)

**Logitech Z906 on Wi-Fi moved out.** The Z906 bridge is not part of the remote and now lives only in its own repository [Aggro600/ESPHomeZ906](https://github.com/Aggro600/ESPHomeZ906) (also in HACS: "Logitech Z906"). The folders z906/ and components/z906/ and the README section are gone from this repository. The IR template "Logitech Z906" for the docks stays. The remote firmware itself is unchanged.

<details><summary>Deutsch</summary>

**Logitech Z906 im WLAN ausgezogen.** Die Z906-Bridge gehört nicht zur Remote und liegt jetzt nur noch im eigenen Repository [Aggro600/ESPHomeZ906](https://github.com/Aggro600/ESPHomeZ906) (auch in HACS: „Logitech Z906“). Die Ordner z906/ und components/z906/ und der Abschnitt im README sind hier entfernt. Die IR-Vorlage „Logitech Z906“ für die Docks bleibt. Die Firmware der Remote ist unverändert.

</details>

## 2026.11.4.5 (2026-11-04)

**HACS review: real entities instead of bare states.** Everything the integration used to publish with plain states is now a sensor or binary sensor with a unique ID: the key configuration per remote (sensor.esphomeremote_config_… and the old …_konfig_… for old firmware), TV input, camera on TV, camera size, timer finished and the mirror sensors of the links. The entity IDs stay the same, so the firmware keeps working; the entities can now be renamed, hidden and assigned to areas and survive a restart.

**Values relayed by the dock get their own entities.** When a remote has no own connection, the dock forwards battery, charging cable, activity and more. These no longer overwrite the remote's ESPHome entities but go to sensor./binary_sensor.esphomeremote_<remote>_<value> (attribute dock). The configurator and the activity switches use them while the ESPHome entity is unavailable; battery history combines both. Automations that relied on the ESPHome entities showing dock values while the remote sleeps should use the new entities. The firmware itself is unchanged.

<details><summary>Deutsch</summary>

**HACS-Prüfung: echte Entitäten statt bloßer Zustände.** Alles, was die Integration bisher als einfachen Zustand gesetzt hat, ist jetzt ein Sensor bzw. Binärsensor mit eindeutiger ID: Tasten-Konfiguration je Remote (sensor.esphomeremote_config_… und das alte …_konfig_… für alte Firmware), TV-Eingang, Kamera auf dem TV, Kamera-Größe, Timer abgelaufen und die Spiegel-Sensoren der Verknüpfungen. Die Entitäts-IDs bleiben gleich, die Firmware arbeitet unverändert; die Entitäten lassen sich jetzt umbenennen, ausblenden und Bereichen zuordnen und überstehen einen Neustart.

**Werte über das Dock bekommen eigene Entitäten.** Hat eine Remote keine eigene Verbindung, leitet das Dock Akku, Ladekabel, Aktivität und mehr weiter. Diese Werte überschreiben nicht mehr die ESPHome-Entitäten der Remote, sondern stehen in sensor./binary_sensor.esphomeremote_<remote>_<wert> (Attribut dock). Konfigurator und Aktivitäts-Schalter nehmen sie, solange die ESPHome-Entität nicht verfügbar ist; der Akku-Verlauf legt beide zusammen. Automationen, die sich darauf verlassen haben, dass die ESPHome-Entitäten im Schlaf Dock-Werte zeigen, sollten die neuen Entitäten nutzen. Die Firmware selbst ist unverändert.

</details>

## 2026.11.4.4 (2026-11-04)

**Home screen editor with the full page tools of the menu editor.** The page header (title, "Arrange automatically", status bar, activity bar) now sits above the preview like in the menu editor, so it stays visible while an entry is selected; before, "Arrange automatically" could not be found then. New there: Subpage, App …, Paste, Page to clipboard, Replace entity and Show on the remote.

**Copy layout to "No activity".** "Copy layout" in the keys editor now works both ways: "Copy here" or "Copy from here to …" one or more activities, including "No activity" of every remote. Copying a single key offers "No activity" too. The firmware itself is unchanged.

<details><summary>Deutsch</summary>

**Startseiten-Editor mit allen Seiten-Werkzeugen des Menü-Editors.** Der Seitenkopf (Titel, „Automatisch anordnen“, Statusleiste, Aktivitäten-Leiste) steht jetzt wie im Menü-Editor über der Vorschau und bleibt sichtbar, wenn ein Eintrag gewählt ist; vorher war „Automatisch anordnen“ dann nicht zu finden. Neu dort: Unterseite, App …, Einfügen, Seite in Zwischenablage, Entität ersetzen und Auf der Remote zeigen.

**Layout nach „Keine Aktivität“ kopieren.** „Layout kopieren“ im Tasten-Editor geht jetzt in beide Richtungen: „Hierher holen“ oder „Von hier kopieren nach …“ in eine oder mehrere Aktivitäten, auch „Keine Aktivität“ jeder Remote. Auch eine einzelne Taste lässt sich nach „Keine Aktivität“ übertragen. Die Firmware selbst ist unverändert.

</details>

## 2026.11.4.3 (2026-11-04)

**HACS review: no more writes to ESPHome's config entries.** With the developer setup (tools/remotes.json) the integration used to point the ESPHome entries of these remotes to the Bluetooth bridge (127.0.0.1:<port>) by itself at every start. It now only checks them: if an entry points elsewhere, a repair issue names host and port. Set them via ESPHome → Reconfigure, or press "Point ESPHome entries to the bridge" in the configurator (Firmware, asks first). Normal installs are not affected. The firmware itself is unchanged.

<details><summary>Deutsch</summary>

**HACS-Prüfung: keine Änderungen mehr an ESPHome-Einträgen.** Im Entwickler-Aufbau (tools/remotes.json) hat die Integration die ESPHome-Einträge dieser Remotes bei jedem Start selbst auf die Bluetooth-Brücke (127.0.0.1:<Port>) gestellt. Jetzt prüft sie nur noch: zeigt ein Eintrag woandershin, nennt ein Reparatur-Hinweis Host und Port. Eintragen über ESPHome → Neu konfigurieren oder im Konfigurator (Firmware) mit „ESPHome-Einträge auf die Brücke stellen“ (fragt vorher). Normale Installationen betrifft das nicht. Die Firmware selbst ist unverändert.

</details>

## 2026.11.4.2 (2026-11-04)

**Configurator:** a remote no longer shows "1 setting pending" forever. A setting the firmware dropped in October (number keyboard, always on since then) could still be stored for a remote; the remote never reports it back. Such settings are now removed on start and ignored when restoring old backups. The firmware itself is unchanged.

<details><summary>Deutsch</summary>

**Konfigurator:** eine Remote zeigt nicht mehr dauerhaft „1 Einstellung unterwegs“. Eine Einstellung, die die Firmware im Oktober abgeschafft hat (Ziffern-Tastatur, seitdem immer an), konnte für eine Remote noch gespeichert sein; die Remote meldet sie nie zurück. Solche Einstellungen werden jetzt beim Start entfernt und beim Einspielen alter Sicherungen ignoriert. Die Firmware selbst ist unverändert.

</details>

## 2026.11.4.1 (2026-11-04)

**Update the remotes' firmware with this version.** Doorbell, camera and cover pictures no longer lie in /config/www (Home Assistant serves that folder to anyone at /local/ without login). They are now in /config/.esphomeremote_images and the remotes fetch them with the integration key; existing pictures are moved on start. Remotes with older firmware show no doorbell, camera or cover pictures until they are updated.

**Security and Home Assistant guidelines (HACS review):**
- The configurator and all commands that change something are admin-only now.
- The integration no longer switches on "Allow the device to perform Home Assistant actions" in ESPHome by itself. If it is off for a remote or dock, a repair notice says where to turn it on.
- Restart via ADB only through the Android TV entity (Android Debug Bridge integration); entries with only an IP are switched to the matching entity automatically. The integration no longer reads that integration's ADB key.
- The translation file is loaded once in the background instead of in the event loop.
- The Streamer app is built from its source in streamer-app/ by a GitHub workflow.

**No activity** now shows the built-in home screen (or the one chosen for "No activity"), no longer the one of the last activity. Needs the new remote firmware.

**Configurator:** home screens can be deleted straight from the list; backups use English keys (version 4, file esphomeremote-backup-…json, older backups can still be restored); IR code export with English keys; missing English translations added.

**Own fixed API key:** the README shows how to replace the OTA password with encryption (id: !extend ota_esphome).

<details><summary>Deutsch</summary>

**Die Firmware der Remotes mit dieser Version aktualisieren.** Klingel-, Kamera- und Coverbilder liegen nicht mehr in /config/www (den Ordner liefert Home Assistant ohne Anmeldung unter /local/ an jeden aus). Sie liegen jetzt in /config/.esphomeremote_images, die Remotes holen sie mit dem Schlüssel der Integration; vorhandene Bilder werden beim Start verschoben. Remotes mit älterer Firmware zeigen bis zum Update keine Klingel-, Kamera- und Coverbilder.

**Sicherheit und Home-Assistant-Regeln (HACS-Prüfung):**
- Konfigurator und alle Befehle, die etwas ändern, nur noch für Admins.
- Die Integration schaltet „Erlaube dem Gerät, Home Assistant-Aktionen durchzuführen“ in ESPHome nicht mehr selbst ein. Ist es bei einer Remote oder einem Dock aus, zeigt ein Reparatur-Hinweis, wo man es einschaltet.
- Neustart per ADB nur noch über die Android-TV-Entität (Integration Android Debug Bridge); Einträge mit nur einer IP werden automatisch auf die passende Entität umgestellt. Die Integration liest den ADB-Schlüssel dieser Integration nicht mehr.
- Die Übersetzung wird einmal im Hintergrund geladen statt in der Ereignisschleife.
- Die Streamer-App baut ein GitHub-Workflow aus ihrem Quellcode in streamer-app/.

**Keine Aktivität** zeigt jetzt die eingebaute Startseite (oder die für „Keine Aktivität“ gewählte), nicht mehr die der letzten Aktivität. Braucht die neue Firmware der Remote.

**Konfigurator:** Startseiten lassen sich direkt in der Liste löschen; Sicherungen mit englischen Schlüsseln (Version 4, Datei esphomeremote-backup-…json, ältere lassen sich weiter einspielen); Export der IR-Codes mit englischen Schlüsseln; fehlende englische Übersetzungen ergänzt.

**Eigener fester API-Schlüssel:** das README zeigt, wie man das OTA-Passwort durch Verschlüsselung ersetzt (id: !extend ota_esphome).

</details>

## 2026.11.3.2 (2026-11-03)

**Popups: new option "Close after a button press".** When on, pressing a button, switch, light or selection closes the popup shortly afterwards (a dropdown only after an entry was picked). Sub pages, sliders, hold buttons and the D-pad in key mode never close it. Off keeps the popup open as before. Needs the new remote firmware.

**Doorbell picture is a normal element now.** The doorbell settings no longer appear on every popup page; add "Doorbell picture" like any other element and its settings (doorbell, trigger, camera, wait time) show up on that page.

**Home Assistant:** file access no longer blocks the event loop (doorbell pictures, IR database download, streamer app), and esp-web-tools 10.4.0 for USB flashing ships with the integration instead of being loaded from a CDN.

<details><summary>Deutsch</summary>

**Popups: neue Option „Nach einem Knopfdruck schließen“.** Ist sie an, geht das Popup nach einem Knopf, Schalter, Licht oder einer Auswahl kurz danach zu (beim Dropdown erst nach der Wahl). Unterseiten, Regler, Halte-Knöpfe und das Steuerkreuz im Tastenmodus schließen es nie. Aus: das Popup bleibt offen wie bisher. Braucht die neue Firmware der Remote.

**Das Klingelbild ist jetzt ein normales Element.** Die Klingel-Einstellungen erscheinen nicht mehr auf jeder Popup-Seite; „Klingelbild“ wie jedes andere Element hinzufügen, dann stehen Klingel, Auslöser, Kamera und Wartezeit bei dieser Seite.

**Home Assistant:** Dateizugriffe blockieren die Ereignisschleife nicht mehr (Klingelbilder, Download der IR-Datenbank, Streamer-App), und esp-web-tools 10.4.0 für das Flashen per USB liegt jetzt in der Integration, statt von einem CDN geladen zu werden.

</details>

## 2026.11.3.1 (2026-11-03)

**Docks: TV pairing no longer gets lost.** ESP-IDF 5.5.6 wiped a paired device's keys from memory on every reconnect, so after the next dropout the TV (seen with Sony) removed the pairing. The dock now keeps the keys. It also no longer asks paired devices for a faster connection interval; the Sony TV dropped the connection every few minutes because of it.

**New menu element: Dropdown** for select and input_select. OK or a tap opens a list with all options, the D-pad picks, OK confirms, Back cancels. Remotes with older firmware show it as a normal selection until they are updated.

**Configurator:** new assistant "D-pad control" for pages and popups (e.g. pan a camera): pick a camera or buttons, it creates the direction entries, stop on release and the toggle button. The key mode is explained more clearly.

**Breaking: service fields and values are English now** (camera: last/off, size: small/medium/large/almost_full/next, light action/value, rules type/step, blaster_send transmitter/carrier/type/repeat, timer_extend minutes, to_remotes service/data, restart device). The old German names keep working and saved layouts are converted automatically. Entities renamed: binary_sensor.esphomeremote_camera_tv, sensor.esphomeremote_camera_size, sensor.esphomeremote_tv_input ("off" instead of "aus") - update automations and dashboards that use them.

<details><summary>Deutsch</summary>

**Docks: Die Kopplung mit dem Fernseher geht nicht mehr verloren.** ESP-IDF 5.5.6 löschte bei jedem Wiederverbinden die Schlüssel eines gekoppelten Geräts aus dem Speicher, nach dem nächsten Abbruch entfernte der Fernseher (gesehen beim Sony) die Kopplung. Das Dock behält die Schlüssel jetzt. Außerdem verlangt es von gekoppelten Geräten keinen schnelleren Verbindungstakt mehr; der Sony-TV trennte deswegen alle paar Minuten.

**Neues Menü-Element: Dropdown** für select und input_select. OK oder Antippen öffnet eine Liste mit allen Optionen, das Steuerkreuz wählt, OK übernimmt, Zurück bricht ab. Remotes mit älterer Firmware zeigen es bis zum Update als normale Auswahl.

**Konfigurator:** neuer Assistent „Steuerkreuz-Steuerung“ für Seiten und Popups (z. B. Kamera schwenken): Kamera oder Tasten wählen, er legt die Richtungs-Einträge, Stopp beim Loslassen und den Umschaltknopf an. Der Tastenmodus ist verständlicher erklärt.

**Achtung: Feldnamen und Werte der Dienste sind jetzt englisch** (camera: last/off, size: small/medium/large/almost_full/next, light action/value, rules type/step, blaster_send transmitter/carrier/type/repeat, timer_extend minutes, to_remotes service/data, restart device). Die alten deutschen Namen funktionieren weiter, gespeicherte Belegungen werden automatisch umgestellt. Umbenannte Entitäten: binary_sensor.esphomeremote_camera_tv, sensor.esphomeremote_camera_size, sensor.esphomeremote_tv_input („off“ statt „aus“) - Automationen und Dashboards, die sie nutzen, bitte anpassen.

</details>

## 2026.11.2.6 (2026-11-02)

**Updating from before 2026.11.2.4 lost your setup? It comes back with this version.** Layouts, settings and own IR codes stayed under the old name in .storage and are now taken over automatically. If the integration is missing, add ESPHomeRemote once: key and remotes are filled in from the old entry. Delete the old folder custom_components/esphomeremote_konfig.

- Backups from older versions can be restored again.
- Backups are now complete: all settings, own IR codes, functions (camera, TV input, restart), dock settings, default settings, factory settings, order of the remotes and locked firmware.
- New remotes start with recommended settings.
- Configurator: buttons are no longer cut off in narrow windows, activities wrap on small screens.

<details><summary>Deutsch</summary>

**Nach dem Update vor 2026.11.2.4 war alles leer? Mit dieser Version kommt es zurück.** Belegungen, Einstellungen und eigene IR-Codes lagen weiter unter dem alten Namen in .storage und werden jetzt automatisch übernommen. Fehlt die Integration, ESPHomeRemote einmal hinzufügen: Schlüssel und Remotes stehen schon im Dialog. Den alten Ordner custom_components/esphomeremote_konfig löschen.

- Sicherungen älterer Versionen lassen sich wieder einspielen.
- Sicherungen sind jetzt vollständig: alle Einstellungen, eigene IR-Codes, Funktionen (Kamera, TV-Eingang, Neustart), Dock-Einstellungen, Standard- und Werkseinstellungen, Reihenfolge der Remotes und gesperrte Firmware.
- Neue Remotes starten mit empfohlenen Einstellungen.
- Konfigurator: Knöpfe werden in schmalen Fenstern nicht mehr abgeschnitten, Aktivitäten brechen auf kleinen Bildschirmen um.

</details>

## 2026.11.2.5 (2026-11-02)

Fixes for the display language on the remote:

- The language setting no longer shows True/False in the configurator, and choosing German is no longer ignored.
- The remote takes the language from the saved configuration at start, so a choice survives a restart.
- English mode no longer crashes the remote: translating all screens blocked the main loop until the watchdog restarted it.

<details><summary>Deutsch</summary>

Korrekturen für die Display-Sprache der Remote:

- Die Spracheinstellung zeigt im Konfigurator nicht mehr True/False, und die Auswahl Deutsch wird nicht mehr ignoriert.
- Die Remote übernimmt die Sprache beim Start aus der gespeicherten Konfiguration, die Auswahl übersteht also einen Neustart.
- Der englische Modus bringt die Remote nicht mehr zum Absturz: Das Übersetzen aller Bildschirme blockierte die Hauptschleife, bis der Watchdog neu startete.

</details>

## 2026.11.2.4 (2026-11-02)

**All names are now English.** The integration domain changed from esphomeremote_konfig to esphomeremote, along with service names (camera_tv, camera_size, light, restart, rules …), ESPHome components (menu, key_config, dock_direct), file names, packages (dock-core, dock-board), tools and docs.

**Breaking:** existing installations must switch folder and domain. Flash docks first, then remotes; a compatibility layer keeps older firmware working in the meantime. Streamer app permissions have to be granted again after updating.

Camera pictures on the remote now show the complete image with black bars instead of being cropped.

<details><summary>Deutsch</summary>

**Alle Namen sind jetzt englisch.** Die Domain der Integration wechselt von esphomeremote_konfig zu esphomeremote, dazu Dienste (camera_tv, camera_size, light, restart, rules …), ESPHome-Komponenten (menu, key_config, dock_direct), Dateien, Pakete (dock-core, dock-board), Werkzeuge und Doku.

**Achtung:** Bestehende Installationen müssen Ordner und Domain umstellen. Erst die Docks flashen, dann die Remotes; eine Übergangsschicht hält ältere Firmware so lange lauffähig. Die Freigaben der Streamer-App müssen nach dem Update neu gesetzt werden.

Kamerabilder auf der Remote zeigen jetzt das ganze Bild mit schwarzen Balken statt eines Ausschnitts.

</details>

## 2026.11.2.3 (2026-11-02)

Validation for HACS and Home Assistant (hassfest) in the repo, preparing for the HACS default list. Logo for the Z906 bridge.

<details><summary>Deutsch</summary>

Prüfungen für HACS und Home Assistant (hassfest) im Repo, Vorbereitung für die HACS-Standardliste. Logo für die Z906-Bridge.

</details>

## 2026.11.2.2 (2026-11-02)

New: **Logitech Z906 on Wi-Fi** as a bonus in the repo (folder z906): ESPHome component for an ESP32 between console and amplifier – volume, levels, input, effect and power state in Home Assistant, the console keeps working. With start-up protection (turning the knob while powering on no longer aborts the start), example configuration and guide.

<details><summary>Deutsch</summary>

Neu: **Logitech Z906 im WLAN** als Extra im Repo (Ordner z906): ESPHome-Komponente für einen ESP32 zwischen Konsole und Verstärker – Lautstärke, Pegel, Eingang, Effekt und Ein/Aus in Home Assistant, die Konsole funktioniert weiter. Mit Startschutz (Drehen am Regler während des Einschaltens bricht den Start nicht mehr ab), Beispielkonfiguration und Anleitung.

</details>

## 2026.11.2.1 (2026-11-02)

New: **Language** – the configurator, the integration and the remote's display are available in English and German. Home Assistant's language applies (otherwise English), selectable in the configurator (bottom left) and per remote ("Display language"). The configurator now lives at /remote (/fernbedienung still works). **Camera on the TV via our own streamer app**: overlay above everything, the running video keeps playing; WebRTC via Home Assistant (otherwise HLS or still images), small/medium/large/almost full screen, corner selectable (new action kamera_position), round card with a thin border; calling it again reopens the camera popup. **Dock stand-in**: no dock? An ESP32, ESP32-S3, ESP32-C6 or bare ESP32-C3 becomes a dock (Bluetooth proxy, Bluetooth keyboard, radio) – the configurator detects the chip when flashing via USB. **Accent colour** instead of blue on the whole remote (Back/X buttons too), set under Settings › Display. Left-handed mode and X button on the right per remote, touchpad and number pad freely arrangeable, gestures only while holding a key, sort the activity bar by dragging. Fix: the dock deleted pairings of unknown addresses (TV had to be re-paired). Fix: Back in the light popup goes up one level.

<details><summary>Deutsch</summary>

Neu: **Sprache** – Konfigurator, Integration und das Display der Remote gibt es auf Deutsch und Englisch. Es gilt die Sprache von Home Assistant (sonst Englisch), wählbar im Konfigurator (unten links) und je Remote („Sprache des Displays“). Der Konfigurator liegt jetzt unter /remote (/fernbedienung geht weiter). **Kamera auf dem Fernseher über die eigene Streamer-App**: Overlay über allem, das laufende Video läuft weiter; WebRTC über Home Assistant (sonst HLS oder Standbilder), Klein/Mittel/Groß/Fast Vollbild, Ecke wählbar (neu: Aktion kamera_position), runde Karte mit dünnem Rand; ein erneuter Aufruf öffnet das Kamera-Popup wieder. **Dock-Nachbau**: kein Dock? Ein ESP32, ESP32-S3, ESP32-C6 oder nackter ESP32-C3 wird zum Dock (Bluetooth-Proxy, Bluetooth-Tastatur, Funk) – der Konfigurator erkennt den Chip beim USB-Flashen. **Akzentfarbe** statt Blau auf der ganzen Remote (auch Zurück-/X-Knöpfe), einstellbar unter Einstellungen › Anzeige. Linkshänder-Modus und X-Knopf rechts je Remote, Touchpad und Ziffernblock frei anordenbar, Gesten nur beim Halten einer Taste, Aktivitäten-Leiste per Ziehen sortieren. Fix: Dock löschte Kopplungen unbekannter Adressen (TV musste neu gekoppelt werden). Fix: Zurück im Licht-Popup springt eine Ebene hoch.

</details>

## 2026.11.1.1 (2026-11-01)

New: **Funktionen** page in the configurator – TV input (sensor reports the real input, e.g. 'HDMI 3'; popups like 'only if TV input is not HDMI 3'), camera on the TV (small/medium/large), restart via ADB, lamp blinks on new firmware, dock watch – no own packages or scripts in HA. Smart Display: control bar on tap (remote/music/clock/off for today/⏮⏯⏭), swipe gestures, parts and order configurable, music view cover/title/bars only. New quick settings: radio path remote → dock → device, tiles dock/Bluetooth/WLAN, bigger panel. Light widget: left/right dims, OK opens, OK long toggles; in the detail view every slider, colour, palette and scene can be selected with the D-pad. Tapping the running activity again opens a popup, app assistant, copy menu/start page to other remotes, readable trigger editor, media card opens a popup, test mode (WLAN stays on, no deep sleep, 12 h). Configurator restructured (all pages in the sidebar, docks with settings), touchpad/digits/keyboard preview like on the display. Radio reviewed: unnecessary maintenance windows removed, configuration via the dock first, HA only sends to reachable docks. Fix: SD card stayed mounted after config mode (microphone/music display blocked). Fix: page scrolled away while dragging in the editor. Fix: light slider got stuck when switching off.

<details><summary>Deutsch</summary>

Neu: Seite **Funktionen** im Konfigurator – TV-Eingang (Sensor meldet den echten Eingang, z. B. „HDMI 3“; Popups wie „Nur wenn TV-Eingang ist nicht HDMI 3“), Kamera auf dem Fernseher (Klein/Mittel/Groß), Neustart per ADB, Lampe blinkt bei neuer Firmware, Dock-Wache – ohne eigene Pakete oder Skripte in HA. Smart Display: Steuerleiste beim Antippen (Remote/Musik/Uhr/Heute aus/⏮⏯⏭), Wischgesten, Teile und Reihenfolge einstellbar, Musik-Ansicht Cover/Titel/nur Balken. Schnelleinstellungen neu: Funkweg Remote → Dock → Gerät, Kacheln Dock/Bluetooth/WLAN, größeres Panel. Licht-Widget: Links/Rechts dimmt, OK öffnet, OK lang schaltet; in der Detailansicht jeder Regler, jede Farbe, Palette und Szene per Steuerkreuz wählbar. Aktivität nochmal antippen öffnet ein Popup, App-Assistent, Menü/Startseite auf andere Remotes übertragen, lesbarer Auslöser-Editor, Medien-Karte öffnet ein Popup, Testmodus (WLAN bleibt an, kein Tiefschlaf, 12 h). Konfigurator neu gegliedert (alle Seiten in der Leiste, Docks mit Einstellungen), Vorschau von Touchpad/Ziffern/Tastatur wie auf dem Display. Funk geprüft: unnötige Wartungsfenster raus, Konfiguration zuerst übers Dock, HA schickt nur an erreichbare Docks. Fix: SD-Karte blieb nach dem Konfig-Modus eingebunden (Mikrofon/Musik-Anzeige gesperrt). Fix: Seite rollte beim Ziehen im Editor weg. Fix: Licht-Regler blieb beim Ausschalten stehen.

</details>

## 2026.10.30.3 (2026-10-30)

Fix: installing over WLAN failed after the build ("_offen() takes 1 positional argument but 2 were given").

<details><summary>Deutsch</summary>

Fix: Installieren per WLAN brach nach dem Bauen ab („_offen() takes 1 positional argument but 2 were given“).

</details>

## 2026.10.30.2 (2026-10-30)

Docks in the configurator like the remotes: in the sidebar with status and short info, expandable with their own pages (overview, Bluetooth, LED, infrared, firmware), order by drag and drop (sidebar and overview), custom colours per dock. Firmware: build queue – build several devices one after another, 'scheduled' builds all, cancel jobs. Fix: remote icons in the sidebar off-centre/too small and state sometimes outdated.

<details><summary>Deutsch</summary>

Docks im Konfigurator wie die Remotes: links in der Leiste mit Status und Kurzinfo, aufklappbar mit eigenen Seiten (Übersicht, Bluetooth, LED, Infrarot, Firmware), Reihenfolge per Ziehen (Leiste und Übersicht), eigene Farben je Dock. Firmware: Warteschlange – mehrere Geräte nacheinander bauen, „Vorgemerkte“ baut alle, Aufträge abbrechen. Fix: Remote-Symbole in der Leiste verschoben/zu klein und Zustand teils veraltet.

</details>

## 2026.10.30.1 (2026-10-30)

Doorbells: any number (up to 8), configured directly in the popup editor, one shared image buffer on the remote. Media widget: new 'card' style (cover as background, volume keys), sources of the 'now playing' player right in the widget; the player forwards play/pause/next/volume to the active source (widgets and card did nothing before). Start page: live display in the editor (custom and built-in), settings on the right, status bar icons in the configurator, one grid with the menu. Activity bar: alignment, corners, gaps, icons per activity, all font sizes. Font sizes 7-28 px freely selectable. Function 'close menu'. Configurator: page tree no longer scrolls away while dragging, icon picker, mini remote with outline.

<details><summary>Deutsch</summary>

Klingeln: beliebig viele (bis 8), eingestellt direkt im Popup-Editor, ein gemeinsamer Bildspeicher auf der Remote. Medien-Widget: neue Darstellung „Karte“ (Cover als Hintergrund, Lauter/Leiser), Quellen des „Jetzt läuft“-Players direkt im Widget; der Player reicht Play/Pause/Weiter/Lautstärke an die laufende Quelle weiter (Widgets und Karte gingen vorher nicht). Startseite: Live-Display im Editor (eigene und eingebaute), Einstellungen rechts, Statusleisten-Symbole im Konfigurator, ein Raster mit dem Menü. Aktivitäten-Leiste: Ausrichtung, Ecken, Fugen, Symbole je Aktivität, alle Schriftgrößen. Schriftgrößen 7–28 px frei wählbar. Funktion „Menü schließen“. Konfigurator: Seitenbaum scrollt beim Ziehen nicht mehr weg, Symbol-Auswahl, Mini-Remote mit Kontur.

</details>

## 2026.10.29.3 (2026-10-29)

New: light widget (single or all lamps, brightness, colour temperature, colour, palette, scenes, sub page) with action esphomeremote_konfig.licht. WLAN: real state with reason and remaining time in quick settings, in the configurator (menu 3/10/30 min, until off, off now, also per remote under firmware) and as sensor; updates keep WLAN on only for the update (optionally 10/30 min longer). Remote order by drag and drop, remote collapsible in the sidebar, popup sub pages shown under their popup. Playback settings as a dialog in menu/start page, streamer app under firmware. Key dots removed, clipboard per session only. Fix: radio to the dock hung after switching WLAN off (no covers, HA calls dropped). Fix: state polling with many entities (buffer).

<details><summary>Deutsch</summary>

Neu: Licht-Widget (Lampen einzeln oder alle, Helligkeit, Farbtemperatur, Farbe, Palette, Szenen, Unterseite) mit Aktion esphomeremote_konfig.licht. WLAN: echter Zustand mit Grund und Restzeit in den Schnelleinstellungen, im Konfigurator (Menü 3/10/30 min, bis aus, jetzt aus, auch je Remote unter Firmware) und als Sensor „WLAN-Zustand“; Updates halten das WLAN nur fürs Update (wahlweise danach noch 10/30 min). Reihenfolge der Remotes per Ziehen, Remote links einklappbar, Popup-Unterseiten im Seitenbaum unter ihrem Popup. Wiedergabe-Einstellungen als Dialog in Menü/Startseite, Streamer-App unter Firmware. Punkte an den Tasten entfernt, Zwischenablage nur für die Sitzung. Fix: Funk zum Dock hing nach dem WLAN-Abschalten (keine Cover, HA-Aufrufe verworfen). Fix: Zustandsabfrage mit vielen Entitäten (Puffer).

</details>

## 2026.10.29.2 (2026-10-29)

Configurator: the power key of the remote view can be colored separately (defaults to the key color).

<details><summary>Deutsch</summary>

Konfigurator: Ein/Aus-Taste der Remote-Ansicht eigens einfärbbar (ohne eigene Farbe wie die übrigen Tasten).

</details>

## 2026.10.29.1 (2026-10-29)

Start page without status bar now fits the grid (7 or 8 rows). New: 'No activity' with its own start page, own keys (unset keys behave like the last activity) and start/stop steps. New: activity at power-on (last, none or a specific one; optionally also after deep sleep). Menu: sliders also in single-row items (from 4 columns). Configurator: colors per remote (view, navigation, overview). Activity bar: colors, font, line, position per start page (bottom/top/off), status bar per start page, 40 px high (6 full rows). Fixed the activity bar build-up animation. Create digits/touchpad popups. Fix: popups (e.g. camera) did not arrive via the dock for 5 min after the remote had been on WLAN (dock firmware). Fix: the WLAN button kept switching the remote's WLAN on for up to 10 min.

<details><summary>Deutsch</summary>

Startseite ohne Statusleiste passt ins Raster (7 bzw. 8 Zeilen). Neu: „Keine Aktivität“ mit eigener Startseite, eigenen Tasten (leere wie die zuletzt gelaufene Aktivität) und Schritten beim Beenden/Starten. Neu: Aktivität beim Einschalten (letzte, keine oder bestimmte; optional auch nach dem Tiefschlaf). Menü: Regler auch einzeilig (ab 4 Spalten). Konfigurator: Farben je Remote (Ansicht, Navigation, Übersicht). Aktivitäten-Leiste: Farben, Schrift, Linie, Lage je Startseite (unten/oben/aus), Statusleiste je Startseite, 40 px hoch (6 volle Zeilen). Aufbau-Animation der Leiste repariert. Popups als Ziffern/Touchpad anlegen. Fix: Popups (z. B. Kamera) kamen nach einer WLAN-Phase der Remote 5 min lang nicht über das Dock (Dock-Firmware). Fix: WLAN-Knopf schaltete das WLAN der Remote bis zu 10 min immer wieder ein.

</details>

## 2026.10.28.4 (2026-10-28)

- **Configurator:** The *Keys* page now shows a faithful front view of the remote (grey front cover, black keys with white symbols, coloured keys). Dots below each symbol show the assignment (short/double/long/without HA); the display shows the activity's start page.
- **Voice search:** While the voice popup is open, all keys (including the D-pad) work normally, just like on the start page.
- **Charging:** If the charge signal flickers after a full charge (charger chip at end of charge), the remote no longer wakes the display each time or counts new charge cycles.

<details><summary>Deutsch</summary>

- **Konfigurator:** Unter *Tasten* zeigt der Konfigurator jetzt eine originalgetreue Vorderansicht der Remote (graues Frontcover, schwarze Tasten mit weißen Symbolen, bunte Farbtasten). Punkte unter dem Symbol zeigen die Belegung (kurz/doppelt/lang/ohne HA), das Display zeigt die Startseite der Aktivität.
- **Sprachsuche:** Solange das Sprach-Popup offen ist, funktionieren alle Tasten (auch das Steuerkreuz) normal wie auf der Startseite.
- **Laden:** Flackert das Ladesignal nach dem Vollladen (Ladechip am Ende der Ladung), schaltet die Remote nicht mehr jedes Mal das Display ein und zählt keine neuen Ladevorgänge.

</details>

## 2026.10.28.3 (2026-10-28)

Fixed update error "menue: expected a dictionary": the firmware package now loads its components straight from GitHub instead of the ESPHome folder – old copies there no longer interfere. Under Firmware the configurator detects old files in the ESPHome folder (components, esphomeremote-*.h, fonts) and moves them to the archive with one click. Remote: when the battery is full on the charger, the flickering charge signal no longer keeps the display on (it could stay lit for hours).

<details><summary>Deutsch</summary>

Update-Fehler „menue: expected a dictionary“ behoben: Das Firmware-Paket holt seine Komponenten jetzt direkt von GitHub statt aus dem ESPHome-Ordner – alte Kopien dort stören nicht mehr. Der Konfigurator erkennt unter Firmware alte Dateien im ESPHome-Ordner (Komponenten, esphomeremote-*.h, Schriften) und räumt sie auf Knopfdruck ins Archiv. Remote: ist der Akku am Kabel voll, flackert die Lade-Meldung nicht mehr das Display an (Display blieb sonst stundenlang an).

</details>

## 2026.10.28.2 (2026-10-28)

- Fresh start: the repository starts over without the old version history (cleaned up)
- Dock: search pause while the TV/streamer reconnects is active again (works around a crash in the ESP32-C3 Bluetooth controller)
- Configurator, docking station: new section "known bugs" – checks GitHub daily whether Espressif has fixed the bug and lets you know when it's worth trying again
- Menu and home screen editor: an entry's options in collapsible groups with a short summary
- A remote's infrared dialog shows all IR databases and your own codes
- Page option "volume up / down / mute control here" removed
- Home screen: the menu button is no longer highlighted after a restart
- Web installer removed for now (returns with ready-built firmware)

<details><summary>Deutsch</summary>

- Neuanfang: Das Repository startet ohne alte Versionsgeschichte neu (aufgeräumt)
- Dock: Suchpause beim Wiederverbinden von TV/Streamer wieder aktiv (umgeht einen Absturz im Bluetooth-Controller des ESP32-C3)
- Konfigurator, Dockingstation: neuer Abschnitt „Bekannte Fehler“ – prüft täglich auf GitHub, ob Espressif den Fehler behoben hat, und meldet sich, wenn es sich lohnt
- Menü- und Startseiten-Editor: Optionen eines Eintrags in aufklappbaren Gruppen mit Kurzinfo
- Infrarot-Dialog einer Remote zeigt alle IR-Datenbanken und Meine Codes
- Seiten-Option „Lauter / Leiser / Stumm steuern hier“ entfernt
- Startseite: nach dem Neustart ist der Menü-Knopf nicht mehr markiert
- Web-Installer vorerst entfernt (kommt mit fertig gebauter Firmware zurück)

</details>

## 2026.10.28.1 (2026-10-28)

- New IR codes page: Flipper IRDB and Logitech Harmony databases (about 258,000 devices) kept in sync with GitHub, with automatic updates; "my codes" to learn, organise, test, collect from your keys, export and import
- Search across all sources from every key as well (filter per source); Harmony codes are recognised as protocols
- Other IR/RF blasters in Home Assistant work like a dock: any infrared entity, Broadlink (IR and 433 MHz), Xiaomi (IR)
- Home screen editor: its own editor like the menu editor, assignment per activity, built-in default page in the same editor; menu without home screens
- Popups with built-in number pad or touchpad
- Widgets: custom home screen lines up with the built-in one, single-line texts, full date
- Streamer app 1.8 with the new logo

<details><summary>Deutsch</summary>

- Neue Seite IR-Codes: Datenbanken Flipper-IRDB und Logitech Harmony (rund 258.000 Geräte) mit Abgleich gegen GitHub und automatischer Aktualisierung; Meine Codes zum Anlernen, Ordnen, Testen, Sammeln aus den Tasten, Export und Import
- Suche in allen Quellen auch in jeder Taste (Filter je Quelle), Harmony-Codes werden als Protokoll erkannt
- Fremde IR-/RF-Sender aus Home Assistant wie ein Dock: jede infrared-Entität, Broadlink (IR und 433 MHz), Xiaomi (IR)
- Startseiten-Editor: eigener Editor wie der Menü-Editor, Zuordnung je Aktivität, eingebaute Standardseite im selben Editor; Menü ohne Startseiten
- Popups mit eingebautem Ziffernblock oder Touchpad
- Widgets: Startseite sitzt wie die eingebaute, Texte einzeilig, Datum vollständig
- Streamer-App 1.8 mit neuem Logo

</details>

## 2026.10.27.3 (2026-10-27)

- Widgets for menu pages: clock, weather (with forecast), battery, media player, timer, camera, combined – all selectable in the configurator
- Own home screen per activity (as free as the menu, or from a template)
- Load factory settings / load default menu / load default keys
- Simpler menu editor: new pages are linked automatically, drag in the tree to move, path display, popup list only shows real popups
- Popups: top-left button can minimise instead of close
- Configurator: turn-on-Wi-Fi button, choose the action before picking an entity, readable dropdowns in dark mode, less flicker/re-rendering, firmware page no longer jumps
- Dock: connection retries, passive scan

<details><summary>Deutsch</summary>

- Widgets für Menüseiten: Uhr, Wetter (mit Vorhersage), Akku, Medien-Player, Timer, Kamera, Kombi – alles im Konfigurator auswählbar
- Eigene Startseite je Aktivität (frei wie das Menü, oder aus Vorlage)
- Werkseinstellung laden / Standardmenü laden / Standardtasten laden
- Menü-Editor einfacher: neue Seiten werden automatisch verlinkt, Ziehen im Baum zum Umhängen, Pfadanzeige, Popups-Liste nur noch mit echten Popups
- Popups: Knopf oben links wahlweise Minimieren statt Schließen
- Konfigurator: WLAN-einschalten-Knopf, Aktion wählen vor der Entitätsauswahl, Dark-Mode-Auswahllisten lesbar, weniger Flackern/Neuaufbau, Firmware-Seite springt nicht mehr
- Dock: Verbindungsaufbau mit Wiederholung, passiver Scan

</details>

## 2026.10.27.2 (2026-10-27)

- **Everything is now called ESPHomeRemote:** `esphomeremote.yaml` / `esphomeremote-firmware.yaml` (remote) and `esphomeremote-dock.yaml` / `esphomeremote-dock-firmware.yaml` (dock); the headers are called `esphomeremote-*.h`. New devices are named `esphomeremote` by default.
- **Existing devices keep building:** the repository still ships the old firmware names (`open-remote-firmware.yaml`, `open-dock-firmware.yaml`) for now. In the configurator under *Firmware*, **Umstellen** (switch over) changes the device file to the new names (the old one is kept as `.alt`). Device name and entities stay as they are.

<details><summary>Deutsch</summary>

- **Alles heißt jetzt ESPHomeRemote:** `esphomeremote.yaml` / `esphomeremote-firmware.yaml` (Fernbedienung) und `esphomeremote-dock.yaml` / `esphomeremote-dock-firmware.yaml` (Dock), die Header heißen `esphomeremote-*.h`. Neue Geräte heißen standardmäßig `esphomeremote`.
- **Bestehende Geräte bauen weiter:** Die alten Firmware-Namen (`open-remote-firmware.yaml`, `open-dock-firmware.yaml`) liefert das Repo vorerst mit. Im Konfigurator unter *Firmware* zeigt **Umstellen** die Gerätedatei auf die neuen Namen um (die alte bleibt als `.alt`). Gerätename und Entitäten bleiben dabei, wie sie sind.

</details>

## 2026.10.27.1 (2026-10-27)

- **Important for HACS:** the repository now contains only the ESPHomeRemote integration. Previously HACS accidentally installed and updated the Bluetooth bridge ("ESPHome BLE-Link") instead of the configurator. Please remove the repository from HACS once and add it again, then download ESPHomeRemote and restart Home Assistant.
- The Bluetooth bridge is now part of ESPHomeRemote (no separate integration any more). If you have `custom_components/esphome_ble_link`: delete it and remove `esphome_ble_link:` from configuration.yaml / your packages.
- New icon and logo (Home Assistant, HACS, GitHub).

<details><summary>Deutsch</summary>

- **Wichtig für HACS:** Im Repo liegt jetzt nur noch die Integration ESPHomeRemote. Vorher hat HACS versehentlich die Bluetooth-Brücke ("ESPHome BLE-Link") statt des Konfigurators installiert und aktualisiert. Bitte das Repo in HACS einmal entfernen und neu hinzufügen, danach ESPHomeRemote herunterladen und Home Assistant neu starten.
- Die Bluetooth-Brücke ist jetzt Teil von ESPHomeRemote (keine eigene Integration mehr). Falls du `custom_components/esphome_ble_link` hast: löschen, und `esphome_ble_link:` aus der configuration.yaml / den Paketen entfernen.
- Neues Icon und Logo (Home Assistant, HACS, GitHub).

</details>

## 2026.10.26.6 (2026-10-26)

Docks are always recognised.

- A dock now counts as a dock even if its name doesn't start with `dock-…`: it is recognised by its firmware (project `aggro600.esphomeremote-dock`). Manually set-up docks therefore appear in the configurator, and the radio link remote → dock and the permission for HA actions apply to them too.
- *Firmware & Geräte* (firmware & devices) finds dock files under any file name.

<details><summary>Deutsch</summary>

Docks werden immer erkannt.

- Ein Dock zählt jetzt auch dann als Dock, wenn es nicht `dock-…` heißt: erkannt an seiner Firmware (Projekt `aggro600.esphomeremote-dock`). Von Hand eingerichtete Docks erscheinen damit im Konfigurator, der Funkweg Remote → Dock und die Freigabe der HA-Aktionen gelten auch für sie.
- *Firmware & Geräte* findet Dock-Dateien unter jedem Dateinamen.

</details>

## 2026.10.26.5 (2026-10-26)

Help with setting up.

- **Repair old files:** under *Firmware & Geräte*, dock and remote files in the old format are marked (dock with `!include templates/dock-basis.yaml`, remote with `@master` without passed-through passwords). **"Auf GitHub-Firmware umstellen"** (switch to GitHub firmware) rewrites them correctly; the old one is kept as `.alt`.
- **Not adopted:** if a remote isn't adopted in Home Assistant, the configurator shows a clear notice at the top with the steps – without this connection, assignment and layout don't arrive.
- The firmware page's hint text now also fits without developer files.

<details><summary>Deutsch</summary>

Hilfe beim Einrichten.

- **Alte Dateien reparieren:** Unter *Firmware & Geräte* sind Dock- und Remote-Dateien in alter Form markiert (Dock mit `!include templates/dock-basis.yaml`, Remote mit `@master` ohne durchgereichte Passwörter). **„Auf GitHub-Firmware umstellen“** schreibt sie richtig um, die alte bleibt als `.alt`.
- **Nicht eingebunden:** Ist eine Remote nicht in Home Assistant eingebunden, zeigt der Konfigurator oben einen deutlichen Hinweis mit den Schritten – ohne diese Verbindung kommen Belegung und Layout nicht an.
- Hinweistext der Firmware-Seite passt jetzt auch ohne Entwickler-Dateien.

</details>

## 2026.10.26.4 (2026-10-26)

No more "Action esphome.…_tasten_neu_laden not found" error.

- After saving, the configurator only nudges the remote if it is currently connected directly to Home Assistant. Otherwise it fetches the new assignment itself the next time it wakes up (via dock or HA).
- All actions on remotes and docks (Bluetooth, infrared test, learning …) now report errors only as an understandable notice in the configurator.
- Note: if the action is permanently missing, the remote isn't adopted in Home Assistant yet – README, step 5.

<details><summary>Deutsch</summary>

Keine Fehlermeldung „Action esphome.…_tasten_neu_laden not found“ mehr.

- Nach dem Speichern stößt der Konfigurator die Remote nur noch an, wenn sie gerade direkt mit Home Assistant verbunden ist. Sonst holt sie sich die neue Belegung beim nächsten Aufwachen selbst (über Dock bzw. HA).
- Alle Aktionen an Remotes und Docks (Bluetooth, Infrarot-Test, Anlernen …) melden Fehler nur noch als verständlichen Hinweis im Konfigurator.
- Hinweis: Fehlt die Aktion dauerhaft, ist die Remote noch nicht in Home Assistant eingebunden – README, Schritt 5.

</details>

## 2026.10.26.3 (2026-10-26)

Setting up without developer files.

- **Docking station:** new include file `open-dock.yaml` – the dock firmware comes straight from GitHub, like the remote's. In the configurator, "Neues Dock" (new dock) creates this form by itself (before: "templates/dock-basis.yaml missing").
- **New remote** in the configurator now also works without a developer setup; if entries are missing in `secrets.yaml`, the assistant tells you which.
- **Remote:** passwords come from your own `secrets.yaml` via `substitutions` (GitHub packages may not read `!secret`) – if you copied `open-remote.yaml` earlier, please take the new version.

<details><summary>Deutsch</summary>

Einrichten ohne Entwickler-Dateien.

- **Dockingstation:** neue Einbinde-Datei `open-dock.yaml` – die Dock-Firmware kommt wie bei der Fernbedienung direkt von GitHub. Im Konfigurator legt „Neues Dock“ diese Form selbst an (vorher: „templates/dock-basis.yaml fehlt“).
- **Neue Fernbedienung** im Konfigurator geht jetzt auch ohne Entwickler-Einrichtung; fehlen Einträge in der `secrets.yaml`, sagt der Assistent welche.
- **Fernbedienung:** Passwörter kommen über `substitutions` aus der eigenen `secrets.yaml` (GitHub-Pakete dürfen kein `!secret` lesen) – wer `open-remote.yaml` früher übernommen hat, bitte die neue Fassung nehmen.

</details>

## 2026.10.26.2 (2026-10-26)

First release with version numbers.

- **Updates for everyone:** Home Assistant announces new versions via HACS; in the configurator under *Firmware*, "Neue Version" shows which remotes and docks are outdated – **Alle aktualisieren** (update all) builds and flashes. Queuing builds right away, "Nur bauen" (build only) for quick flashing later.
- **Firmware from GitHub:** `open-remote.yaml` fetches it from the `stabil` branch; passwords come from your own `secrets.yaml` (take the new version of `open-remote.yaml`).
- **Streamer app 1.7:** start page on the TV (status, uninstall); set up, update and remove it in the configurator – the app ships with the integration. Cursor ‹ › in text fields.
- **Media popup** made of separate, freely movable elements, popup templates, tile options (layout, second line, badge, colours per state, condition, long press, confirmation …).
- **Timer:** several selectable, popup "timer finished" with +5 min and again.
- **Keyboard:** holding repeats, keyboard popup more reliable and faster.
- **Configurator:** copy and paste in parts, take parts from other remotes, backup with settings, new battery page, dock traffic light, home screen buttons with their own target.

<details><summary>Deutsch</summary>

Erster Release mit Versionsnummern.

- **Updates für alle:** Home Assistant meldet neue Versionen über HACS; im Konfigurator unter *Firmware* zeigt „Neue Version“, welche Fernbedienungen und Docks veraltet sind – **Alle aktualisieren** baut und spielt auf. Vormerken baut sofort, „Nur bauen“ für schnelles Flashen danach.
- **Firmware von GitHub:** `open-remote.yaml` holt sie vom Zweig `stabil`; Passwörter kommen aus der eigenen `secrets.yaml` (neue Fassung von `open-remote.yaml` übernehmen).
- **Streamer-App 1.7:** Start-Seite am Fernseher (Status, Deinstallieren), im Konfigurator einrichten, aktualisieren und entfernen – die App kommt mit der Integration. Cursor ‹ › im Eingabefeld.
- **Medien-Popup** aus einzelnen, frei verschiebbaren Elementen, Popup-Vorlagen, Kachel-Optionen (Anordnung, zweite Zeile, Abzeichen, Farben je Zustand, Bedingung, Lang drücken, Sicherheitsabfrage …).
- **Timer:** mehrere auswählbar, Popup „Timer abgelaufen“ mit +5 min und Nochmal.
- **Tastatur:** Halten wiederholt, Tastatur-Popup zuverlässiger und schneller.
- **Konfigurator:** Kopieren und Einfügen in Teilen, Teile von anderen Remotes übernehmen, Sicherung mit Einstellungen, neue Akku-Seite, Dock-Ampel, Startseiten-Knöpfe mit eigenem Ziel.

</details>
