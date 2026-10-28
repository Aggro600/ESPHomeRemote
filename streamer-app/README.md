🇬🇧 English · 🇩🇪 [Deutsch](LIESMICH.md)

# ESPHomeRemote Streamer (Android TV)

A small app for Android TV streamers (e.g. Google TV Streamer, Chromecast with Google TV, Shield). It tells
Home Assistant what is playing - every app with a media session: title, artist, cover, position, duration - and
whether the on-screen keyboard is open (keyboard popup on the remote). In the TV's app list,
"ESPHomeRemote Streamer" shows whether everything is running (with a button to uninstall).

Easiest via the configurator (the app ships with the integration):
1. On the streamer, unlock the developer options (Settings > System > About > tap "Build" 7 times) and turn on
   USB debugging.
2. In Home Assistant, add the "Android Debug Bridge" (androidtv) integration with the streamer's IP and confirm
   "Always allow" on the TV.
3. Configurator > Wiedergabe (playback) > Streamer anlegen (add streamer), pick the streamer as ADB device, tap
   **Einrichten** (set up). This installs the app, enables its services and connects it to Home Assistant. Updates
   come the same way later ("Auf ... aktualisieren", update to ...), as does "Deinstallieren" (uninstall).

By hand: `adb install esphomeremote-streamer.apk`, then
   `adb shell cmd notification allow_listener de.esphomeremote.streamer/.Medien`
   `adb shell settings put secure enabled_accessibility_services de.esphomeremote.streamer/.Bedienung`
   `adb shell settings put secure accessibility_enabled 1`
   and in the configurator **Am Streamer eintragen** (register on the streamer).

Build it yourself: `sh bauen.sh` (JDK 17, Android build-tools 35, platform 35; paths in the script).
