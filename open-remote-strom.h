#pragma once
// Reine Rechenfunktionen fuer die Ruhe-Zeiten der Fernbedienung (Energie sparen).
// Bewusst OHNE Zugriff auf ESPHome-Globals - die uebergibt der Aufrufer.
// "low" = Sparmodus bei niedrigem Akku aktiv: alle Zeiten werden gekappt.
namespace remote_strom {
inline int eff_wifi_s(int base, bool low) { return low ? 0 : base; }
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
}  // namespace remote_strom
