#pragma once
// Bedingungen fuer den Tiefschlaf (Einstellungen > Energie & Wecken). Steht in esphome: includes: und damit in
// main.cpp hinter allen Objekten - die YAML-ids sind hier als Zeiger sichtbar.

// Was haelt die Remote gerade vom Tiefschlaf ab? nullptr = nichts (Zeitablauf prueft deep_ruhe_timer selbst).
// mit_ble: auch pruefen, ob ein Bluetooth-Geraet verbunden ist ("Tiefschlaf nur ohne Bluetooth").
// Gleichzeitig Diagnose-Text fuer den Sensor "Tiefschlaf-Status" (schlaf_status_melden, 2026-10-19).
inline const char *tiefschlaf_sperre(bool mit_ble = true) {
  const bool low = low_bat->value();
  if (!disp_off->value()) return "Display an";
  if (!(ps_deep->value() || low)) return "Tiefschlaf aus (Einstellung)";
  if (mit_ble && deep_ohne_ble->value() && ble_paired->state && !low) return "Bluetooth-Geraet verbunden (Einstellung)";
  if (remote_strom::eff_deep_s(ps_deep_s->value(), ps_deep_tv_s->value(), tv_on->value(), low) < 0) return "Fernseher an (Einstellung: nie)";
  if (testmodus->value()) return "Testmodus";
  if (boot_grace->value()) return "Schonfrist nach dem Start (2 min)";
  // Erst einmal erreichbar sein (fuer HA, Updates). Seit dem Funkkonzept "Dock, WLAN als Ausweg" zaehlt auch das Dock -
  // ohne das blieb Remote 1 eine ganze Nacht wach, weil das WLAN nie kam (2026-10-26). Hoechstens 10 min warten.
  if (!api_ever->value() && direkt->ok_ms() == 0 && !zustand_da->value() && millis() < 600000)
    return "noch nicht mit Home Assistant verbunden";
  if (laedt->value() || charger_present->state) return "laedt";
  if (voice_active->value()) return "Sprachabfrage";
  if (host_mic->value()) return "Mikrofon vom Bluetooth-Geraet offen";
  if (ota_aktiv->value()) return "Update/Wartung";
  if ((int32_t) (millis() - wartung_bis->value()) < 0) return "Wartungsfenster";
  return nullptr;
}

// Darf die Remote jetzt in den Tiefschlaf? (Zeitablauf prueft deep_ruhe_timer selbst.)
inline bool tiefschlaf_erlaubt(bool mit_ble = true) { return tiefschlaf_sperre(mit_ble) == nullptr; }

// Letzte Messung aus dem FIFO (Bewegungsgesten, esphomeremote-gesten.h): Ist der FIFO an, liefern die Ausgangsregister
// die aelteste Messung und jedes Lesen nimmt den Gesten eine weg - dann gilt diese hier.
struct LetzteLage { float x{0}, y{0}, z{0}; uint32_t ms{0}; };
inline LetzteLage &letzte_lage() { static LetzteLage l; return l; }

// Lage aus dem Bewegungssensor (LIS3DH, Register 0x28.., 12 Bit links-ausgerichtet) - fuer Hochheben/Kippen
inline bool lage_lesen(float &x, float &y, float &z) {
  if (motion_chip->fifo()) {
    const auto &l = letzte_lage();
    if (l.ms == 0 || millis() - l.ms > 500) return false;
    x = l.x; y = l.y; z = l.z;
    return true;
  }
  uint8_t d[6];
  if (!motion_chip->read_bytes(0x28 | 0x80, d, 6)) return false;
  x = ((int16_t) ((uint16_t) d[1] << 8 | d[0])) >> 4;
  y = ((int16_t) ((uint16_t) d[3] << 8 | d[2])) >> 4;
  z = ((int16_t) ((uint16_t) d[5] << 8 | d[4])) >> 4;
  return true;
}
