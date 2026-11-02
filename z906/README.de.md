🇬🇧 [English](README.md) · 🇩🇪 Deutsch

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="logo-dunkel.png">
    <img src="logo.png" alt="Z906 Bridge" width="420">
  </picture>
</p>

# Logitech Z906 im WLAN (ESPHome)

Ein ESP32 sitzt zwischen Konsole und Verstärker eines Logitech Z906 und bringt die Anlage in Home Assistant –
**ohne** die Konsole zu ersetzen. Die Konsole funktioniert genau wie vorher; der ESP32 reicht jedes Byte in
beide Richtungen durch und liest mit.

**In Home Assistant:** Lautstärke, Subwoofer, Center und Rear (Zahl, %), Eingang 1–6 und Effekt
(3D / 2.1 / 4.1 / aus) als Auswahl, Ein/Aus-Zustand, Knopf „Status lesen“.

**Was sie erledigt**
- Reicht vom ersten Moment an alles durch (eigene Task) – die Konsole startet unabhängig davon, wann der
  IR-Befehl oder der Einschaltknopf kommt.
- **Startschutz:** Am Lautstärkeregler drehen, während die Konsole hochfährt, brach den Start der Konsole ab.
  Pegel-Schritte während des Weckvorgangs werden zurückgehalten, und die Bridge sendet 4 s nach dem Einschalten
  nichts Eigenes (die Konsole liest dann selbst den Status).
- Befehle aus Home Assistant gehen nur raus, wenn die Leitung frei ist und keine Antwort aussteht (der Verstärker
  verträgt keine überlappenden Anfragen).
- Eingangswechsel wie die Konsole: stumm, Eingang wechseln, Effekt des Eingangs, wieder laut.
- Störfilter für den Datenmüll, den der Verstärker beim Hochfahren sendet.

**Einrichten**
1. ESP32 anschließen wie oben in [`logi-z906.yaml`](logi-z906.yaml) beschrieben (DE-15 zwischen Konsole und
   Verstärker, eigenes USB-Netzteil für den ESP32).
2. `logi-z906.yaml` in den ESPHome-Ordner kopieren, `api_key_z906`, `ota_pw`, `wifi_ssid` und `wifi_password`
   in die `secrets.yaml` eintragen und installieren. Die Komponente kommt direkt von GitHub (`external_components`).
3. Das Gerät in Home Assistant einbinden (ESPHome-Integration).

Protokoll: [nomis/logitech-z906](https://github.com/nomis/logitech-z906/blob/main/protocol.rst).
Idee und Verkabelung: [Jupsi/logi_z906_wifi](https://github.com/Jupsi/logi_z906_wifi).

> Bekanntes Problem an einem Gerät: Der Wechsel auf einen Eingang mit 3D-Effekt ließ den Verstärker neu starten –
> auch mit älterer Firmware der Bridge. Wer das auch sieht: bitte einmal mit der Konsole direkt am Verstärker
> (ohne Bridge) testen und das Ergebnis melden.
