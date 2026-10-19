# ESPHomeRemote Streamer (Android TV)

Kleine App fuer Android-TV-Streamer (z. B. Google TV Streamer, Chromecast mit Google TV, Shield). Sie meldet
Home Assistant, was laeuft - jede App mit Medien-Sitzung: Titel, Interpret, Cover, Position, Dauer - und ob die
Bildschirmtastatur offen ist (Tastatur-Popup der Fernbedienung).

1. Im Konfigurator **Wiedergabe > Streamer anlegen**, als ADB-Geraet die Android-TV-Integration des Streamers waehlen.
2. `esphomeremote-streamer.apk` installieren: `adb install esphomeremote-streamer.apk`
3. In den Einstellungen des Streamers die Dienste der App erlauben (Benachrichtigungszugriff bzw. Bedienungshilfe),
   oder per ADB:
   `adb shell cmd notification allow_listener de.esphomeremote.streamer/.Medien`
   `adb shell settings put secure enabled_accessibility_services de.esphomeremote.streamer/.Bedienung`
   `adb shell settings put secure accessibility_enabled 1`
4. Im Konfigurator **Am Streamer eintragen** druecken (setzt die Webhook-Adresse per ADB) - oder von Hand:
   `adb shell settings put global esphomeremote_webhook http://<HA>:8123/api/webhook/<Kennung aus dem Konfigurator>`

Selbst bauen: `sh bauen.sh` (JDK 17, Android build-tools 35, platform 35; Pfade im Skript).
