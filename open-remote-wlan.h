// WLAN-Schnellstart (2026-09-30): Zugriff auf die Liste der WLAN-Netze (WiFiComponent::sta_),
// um die feste IP vor dem WLAN-Start abzuschalten (Schalter "WLAN-Schnellstart" -> DHCP).
// ESPHome bietet dafuer vor setup() keinen oeffentlichen Weg (get_sta() liefert dort noch nichts,
// die Klasse ist final). Standard-Kniff: explizite Template-Instanziierung darf private/protected
// Member benennen.
#pragma once
#include "esphome/components/wifi/wifi_component.h"

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
}  // namespace wlan_zugriff

// Boot-Zeitplan (Messung 2026-09-30): millis() je on_boot-Stufe merken, spaeter als Text senden.
#include <string>
#include <cstdio>
#include "esp_timer.h"
#include "esp_cpu.h"
#include "lvgl.h"
#include "esp_rtc_time.h"
inline std::string &boot_plan_puffer() { static std::string s; return s; }
inline void boot_mark(const char *name) {
  char b[24];
  // esp_timer statt millis(): millis() laeuft vor dem Scheduler-Start noch nicht.
  snprintf(b, sizeof(b), "%s%s:%.2f", boot_plan_puffer().empty() ? "" : " ", name, esp_timer_get_time() / 1e6f);
  boot_plan_puffer() += b;
}
inline std::string boot_plan_text() { return boot_plan_puffer(); }

// WLAN-Ablauf (Messung 2026-09-30): Zeitpunkte der IDF-WLAN-Ereignisse in den Boot-Zeitplan.
#include "esp_event.h"
#include "esp_wifi.h"
// Dauer vom WLAN-Start (auch nach "WLAN aus im Ruhezustand") bis zur IP, in ms.
inline int64_t &wlan_start_us() { static int64_t t = 0; return t; }
inline uint32_t &wlan_dauer_ms() { static uint32_t d = 0; return d; }
inline void wlan_ereignis_(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == IP_EVENT) {
    if (wlan_start_us()) wlan_dauer_ms() = (esp_timer_get_time() - wlan_start_us()) / 1000;
    if (boot_plan_puffer().size() < 200) boot_mark("ip");
    return;
  }
  if (boot_plan_puffer().size() >= 200 && id != WIFI_EVENT_STA_START) return;   // nur der Boot interessiert
  switch (id) {
    case WIFI_EVENT_STA_START: wlan_start_us() = esp_timer_get_time(); if (boot_plan_puffer().size() < 200) boot_mark("sta_start"); break;
    case WIFI_EVENT_SCAN_DONE: boot_mark("scan"); break;
    case WIFI_EVENT_STA_CONNECTED: {
      auto *c = static_cast<wifi_event_sta_connected_t *>(data);
      char b[24]; snprintf(b, sizeof(b), "assoc(ch%u,auth%u)", c->channel, (unsigned) c->authmode);
      boot_mark(b); break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      auto *d = static_cast<wifi_event_sta_disconnected_t *>(data);
      char b[16]; snprintf(b, sizeof(b), "trenn%u", d->reason);
      boot_mark(b); break;
    }
    default: break;
  }
}
inline void wlan_ereignisse_messen() {
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wlan_ereignis_, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wlan_ereignis_, nullptr);
}

// NVS-Belegung (Messung 2026-09-30): viele Eintraege verlangsamen nvs_flash_init() beim Start.
#include "nvs.h"
#include "esp_heap_caps.h"
inline std::string nvs_statistik() {
  nvs_stats_t st{};
  if (nvs_get_stats(nullptr, &st) != ESP_OK) return "nvs ?";
  char b[80];
  snprintf(b, sizeof(b), " nvs:%u int:%uk/%uk ps:%uk", (unsigned) st.used_entries,
           (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) ((heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) / 1024));
  return b;
}
inline int boot_mark_i(const char *n) { boot_mark(n); return 0; }
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

// ---- Mikrofon-Selbsttest (2026-10-01, Diagnose per HA-Dienst mic_selbsttest) ------------------
// Zaehlt Rohdaten des I2S-Mikrofons (32-bit-Samples): Anzahl, Minimum, Maximum, Mittel des Betrags.
struct MicTest { volatile bool an = false; uint32_t n = 0, null = 0; int32_t mn = 0, mx = 0; uint64_t sum = 0; uint32_t bytes = 0; };
inline MicTest &mic_test() { static MicTest m; return m; }
inline void mic_test_daten(const std::vector<uint8_t> &d) {
  MicTest &m = mic_test();
  if (!m.an) return;
  m.bytes += d.size();
  for (size_t i = 0; i + 3 < d.size(); i += 4) {
    int32_t s; memcpy(&s, &d[i], 4);
    s >>= 8;
    if (m.n == 0) { m.mn = m.mx = s; }
    if (s < m.mn) m.mn = s;
    if (s > m.mx) m.mx = s;
    if (s == 0) m.null++;
    m.sum += (uint64_t) (s < 0 ? -(int64_t) s : s);
    m.n++;
  }
}
