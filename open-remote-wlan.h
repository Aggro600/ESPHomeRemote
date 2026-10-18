// WLAN-Schnellstart (2026-09-30): Zugriff auf die Liste der WLAN-Netze (WiFiComponent::sta_),
// um die feste IP vor dem WLAN-Start abzuschalten (Schalter "WLAN-Schnellstart" -> DHCP).
// ESPHome bietet dafuer vor setup() keinen oeffentlichen Weg (get_sta() liefert dort noch nichts,
// die Klasse ist final). Standard-Kniff: explizite Template-Instanziierung darf private/protected
// Member benennen.
#pragma once
#include "esphome/components/wifi/wifi_component.h"

#include <functional>
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
