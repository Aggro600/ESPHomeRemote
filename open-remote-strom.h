#pragma once
#include <cstdint>
// Reine Rechenfunktionen fuer die Ruhe-Zeiten der Sabrina-Remote (Energie sparen).
// Bewusst OHNE Zugriff auf ESPHome-Globals - die uebergibt der Aufrufer.
// "low" = Sparmodus bei niedrigem Akku aktiv: alle Zeiten werden gekappt.
namespace remote_strom {
// WLAN aus: wie eff_deep_s (tv_s: 0 = nie waehrend der Fernseher laeuft, -1 = wie oben,
// >0 = eigene Zeit) - Nutzerwunsch 2026-09-28, gleiche Unterscheidung wie beim Tiefschlaf.
// Akku-Sparmodus hat Vorrang vor der TV-Zeit: dann weiterhin sofort (0), wie bisher.
inline int eff_wifi_s(int base, int tv_s, bool tv_on, bool low) {
  if (low) return 0;
  if (tv_on && tv_s == 0) return -1;
  if (tv_on && tv_s > 0) return tv_s;
  return base;
}
inline int eff_cpu_s(int base, bool low)  { return low ? 0 : base; }
inline int eff_ble_s(int base, bool low)  { return (low && base > 5) ? 5 : base; }
inline int eff_disp_s(int base, bool low) { return (low && base > 15) ? 15 : base; }
// Tiefschlaf: Sekunden ab Display-Aus. -1 = gesperrt (Fernseher laeuft und
// "Tiefschlaf bei TV an" = nie). tv_s: 0 = nie, -1 = wie ohne TV, >0 = eigene Zeit.
inline int eff_deep_s(int base, int tv_s, bool tv_on, bool low) {
  int s = base;
  if (tv_on && !low) {
    if (tv_s == 0) return -1;
    if (tv_s > 0) s = tv_s;
  }
  if (low && s > 30) s = 30;
  return s;
}
// Schwellen des Bewegungs-Chips (LIS3DH, ~16 mg je Stufe). Index = Empfindlichkeit:
// 0 = Hoch, 1 = Mittel, 2 = Niedrig. Werte ausserhalb werden auf Mittel gesetzt.
// Ruck: Beschleunigung ohne Schwerkraft (Hochpass), 128 / 256 / 448 mg.
inline uint8_t ruck_ths(int sens)  { static const uint8_t t[3] = {0x08, 0x10, 0x1C}; return t[(sens >= 0 && sens <= 2) ? sens : 1]; }
// Klopfen: Spitze eines einzelnen Klopfers, 320 / 512 / 768 mg.
inline uint8_t klopf_ths(int sens) { static const uint8_t t[3] = {0x14, 0x20, 0x30}; return t[(sens >= 0 && sens <= 2) ? sens : 1]; }
}  // namespace remote_strom
