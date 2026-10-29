// WLAN-Schnellstart (2026-09-30): Zugriff auf die Liste der WLAN-Netze (WiFiComponent::sta_),
// um die feste IP vor dem WLAN-Start abzuschalten (Schalter "WLAN-Schnellstart" -> DHCP).
// ESPHome bietet dafuer vor setup() keinen oeffentlichen Weg (get_sta() liefert dort noch nichts,
// die Klasse ist final). Standard-Kniff: explizite Template-Instanziierung darf private/protected
// Member benennen.
#pragma once
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstring>
#include <functional>
#include <string>
namespace wlan_zugriff {
struct Sta {
  using type = esphome::FixedVector<esphome::wifi::WiFiAP> esphome::wifi::WiFiComponent::*;
  friend type get(Sta);
};
template<typename Tag, typename Tag::type M> struct Rob {
  friend typename Tag::type get(Tag) { return M; }
};
template struct Rob<Sta, &esphome::wifi::WiFiComponent::sta_>;

// Entfernt die feste IP aus allen konfigurierten Netzen (-> DHCP). Nur vor dem WLAN-Start aufrufen.
inline void dhcp_statt_fester_ip() {
  auto *w = esphome::wifi::global_wifi_component;
  if (w == nullptr) return;
#ifdef USE_WIFI_MANUAL_IP   // ohne manual_ip in der YAML (IP vom Router) gibt es nichts abzuschalten
  for (auto &ap : w->*get(Sta{})) ap.set_manual_ip({});
#endif
}
// "Nur Bluetooth" (ha_tunnel, nur in den -halink-Konfigurationen): solange true, schaltet die Remote das WLAN
// nicht selbst ein (Aufwachen, Taste, Sparschalter). Sonst kaempfen Remote und Tunnel ums WLAN (2026-10-05).
inline bool g_nur_ble = false;
// Sprache ueber den Bluetooth-Tunnel (ha_tunnel, nur -halink): die -halink-Konfiguration setzt die Haken. Leer = aus.
inline std::function<bool()> g_link_voice_start;
inline std::function<void()> g_link_voice_stop;
// Home Assistant ueber den Bluetooth-Tunnel verbunden (ha_tunnel, nur -halink; Farbe des HA-Symbols). Leer = nie.
inline std::function<bool()> g_link_ha;
// Schalter "Nur Bluetooth" im Menue (Energie sparen): nur sichtbar, wenn der Tunnel den Haken setzt.
inline std::function<void(bool)> g_set_nur_ble;
// WLAN-Bedarf (2026-10-25, Funkkonzept "Dock, WLAN als Ausweg"): bei "WLAN aus" darf das WLAN bis g_bedarf_bis an
// sein - kein Dock eingerichtet oder erreicht, Sprache an HA, Firmware-Update. Die -halink-Konfiguration gibt es
// dem Tunnel als "WLAN erlaubt" weiter.
inline uint32_t g_bedarf_bis = 0;
// Neuer Bedarf (WLAN war aus): HA ueber das Dock vorwarnen, damit es sofort verbindet statt erst beim naechsten
// 15-s-Versuch (2026-10-25, Sprache an HA lief vorher in die 12-s-Frist). Gesetzt in remote.yaml.
inline std::function<void()> g_bedarf_neu;
// WLAN-Zustand (2026-10-29, Nutzer: "verwirrend"): Grund und Restzeit fuer Schnelleinstellungen, HA und Konfigurator.
// g_bedarf_grund: letzter Bedarf; g_fenster_*: naechstes Wartungsfenster (Dauer/Grund, vor script wartungsfenster setzen);
// g_still_bis: nach "WLAN jetzt aus" 10 min keinen automatischen Bedarf (sonst ging es bei Dock weg gleich wieder an).
inline const char *g_bedarf_grund = "";
inline uint32_t g_fenster_s = 180;
inline std::string g_fenster_grund = "Wartung";
inline std::string g_aktiv_grund = "Wartung";   // Grund des laufenden Fensters (Anzeige)
inline uint32_t g_still_bis = 0;
inline std::string g_zustand = "aus";   // aus | immer | geht aus | <Grund> (dann g_rest Sekunden)
inline uint32_t g_rest = 0;
inline void fenster(uint32_t sek, const char *grund) { g_fenster_s = sek; g_fenster_grund = grund; }
// Kurzname fuer die Kachel der Schnelleinstellungen
inline const char *kurz(const std::string &g) {
  if (g.find("kein Dock") != std::string::npos) return "kein Dock";
  if (g.find("Dock") != std::string::npos) return "Dock weg";
  if (g.find("Sprache") != std::string::npos) return "Sprache";
  if (g.find("Konfiguration") != std::string::npos || g.find("Bild") != std::string::npos) return "Laden";
  if (g == "Konfigurator") return "Konfig.";
  if (g == "Ladekabel") return "Laden";
  return g.c_str();
}
inline void bedarf(uint32_t ms, const char *grund) {
  if ((int32_t) (esphome::millis() - g_still_bis) < 0 && strstr(grund, "Sprache") == nullptr) return;
  const uint32_t bis = esphome::millis() + ms;
  const bool neu = (int32_t) (esphome::millis() - g_bedarf_bis) >= 0;
  if (neu || (int32_t) (bis - g_bedarf_bis) > 0) { g_bedarf_bis = bis; g_bedarf_grund = grund; }
  if (neu) {
    ESP_LOGI("wlan", "WLAN-Bedarf: %s (%u s)", grund, (unsigned) (ms / 1000));
    if (g_bedarf_neu) g_bedarf_neu();
  }
}
inline bool bedarf_aktiv() { return g_bedarf_bis != 0 && (int32_t) (esphome::millis() - g_bedarf_bis) < 0; }
}  // namespace wlan_zugriff

#include <string>
#include "lvgl.h"
// ---- Schnellerer Seitenaufbau beim Start (2026-09-30) ------------------------------------------
// LVGL 9.5: jedes Label, dessen Text sich aendert, haengt einen Rueckruf an das DISPLAY
// (LV_EVENT_UPDATE_LAYOUT_COMPLETED), der erst beim naechsten Layout-Durchlauf wieder entfernt wird.
// Beim Booten entstehen ~600 Labels, bevor je ein Layout laeuft -> die Liste wird lang, und JEDES
// Display-Ereignis (LV_EVENT_REFR_REQUEST bei jeder Stil-/Layout-Aenderung) laeuft sie komplett ab:
// quadratischer Aufwand, gemessen 4,3 s fuer den Seitenaufbau. Ein unsichtbares Label am Ende jeder
// Seite ruft diese Funktion beim Erzeugen auf (text: !lambda) und arbeitet die Liste ab -> 1,15 s.
inline std::string lv_label_rueckrufe_leeren() {
  lv_display_t *d = lv_display_get_default();
  if (d != nullptr) lv_display_send_event(d, LV_EVENT_UPDATE_LAYOUT_COMPLETED, nullptr);
  return std::string();
}
