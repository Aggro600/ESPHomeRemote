🇬🇧 [English](README.md) · 🇩🇪 Deutsch

# ESPHomeRemote Streamer (Android TV)

Kleine App fuer Android-TV-Streamer (z. B. Google TV Streamer, Chromecast mit Google TV, Shield). Sie meldet
Home Assistant, was laeuft - jede App mit Medien-Sitzung: Titel, Interpret, Cover, Position, Dauer - und ob die
Bildschirmtastatur offen ist (Tastatur-Popup der Fernbedienung). In der App-Liste des Fernsehers zeigt sie unter
"ESPHomeRemote Streamer", ob alles laeuft (mit Knopf zum Deinstallieren).

Am einfachsten im Konfigurator (die App kommt mit der Integration mit):
1. Am Streamer die Entwickleroptionen freischalten (Einstellungen > System > Ueber > 7x auf "Build") und
   USB-Debugging einschalten.
2. In Home Assistant die Integration "Android Debug Bridge" (androidtv) mit der IP des Streamers hinzufuegen und am
   Fernseher "Immer zulassen" bestaetigen.
3. Konfigurator > Wiedergabe > Streamer anlegen, als ADB-Geraet den Streamer waehlen, auf **Einrichten** tippen.
   Das spielt die App auf, schaltet ihre Dienste frei und verbindet sie mit Home Assistant. Updates kommen spaeter
   genauso ("Auf ... aktualisieren"), ebenso "Deinstallieren".

Von Hand: `adb install esphomeremote-streamer.apk`, dann
   `adb shell cmd notification allow_listener de.esphomeremote.streamer/.Medien`
   `adb shell settings put secure enabled_accessibility_services de.esphomeremote.streamer/.Bedienung`
   `adb shell settings put secure accessibility_enabled 1`
   und im Konfigurator **Am Streamer eintragen**.

Selbst bauen: `sh bauen.sh` (JDK 17, Android build-tools 35, platform 35; Pfade im Skript).
