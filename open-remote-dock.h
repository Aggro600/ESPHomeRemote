#pragma once
// Dock-Anzeige / "Smart Display" (2026-10-17): Steht die Remote im Dock, zeigt sie statt der Fernbedienung
// Uhrzeit, Wetter, Raumklima, die naechsten Termine und den Ladestand (Seite page_dock, cur_page 50).
//
// Im Dock?  Das Ladekabel (Pogo-Pins) meldet sich -> die Lage (Bewegungssensor) wird als "Dock-Lage" gemerkt.
// Solange sie bleibt, gilt die Remote als im Dock - auch wenn der Lade-IC bei vollem Akku "nicht mehr laden"
// meldet. Weicht die Lage deutlich ab (herausgenommen), sofort zurueck zur Fernbedienung.
// Taste oder Tipp im Dock: die Fernbedienung erscheint; 20 s nach der letzten Eingabe wieder die Dock-Anzeige.
// Display im Dock (Einstellung dock_disp): 0 = wie sonst (geht nach der Zeit aus), 1 = bleibt an,
// 2 = bleibt an zwischen dock_von und dock_bis Uhr. Helligkeit wie immer (Lichtsensor-Kurve).
// Daten: GET /api/esphomeremote_konfig/anzeige/<geraet> (HA, anzeige.py), jede Minute solange sichtbar.

#include <esp_http_client.h>

// Musik-Anzeige (open-remote-musik.h, danach eingebunden): im Dock statt des Smart Displays, solange Musik laeuft
namespace musik {
inline bool im_dock_zeigen();
inline void zeigen(bool vom_dock);
inline void dock_takt(bool im_dock);
}

namespace dockanz {

struct Zustand {
  bool im_dock{false};
  float lx{0}, ly{0}, lz{0};
  bool lage_ok{false};
  int weg{0};
  uint32_t kabel_seit{0};
  bool kabel_vorher{false};
  uint32_t wach_ms{0};
  uint32_t abruf_ms{0};
  bool gebaut{false};
  lv_obj_t *datum{nullptr}, *uhr{nullptr}, *w_ico{nullptr}, *w_temp{nullptr}, *w_text{nullptr}, *raum{nullptr},
      *raum_ico{nullptr}, *k_box{nullptr}, *lade{nullptr}, *lade_ico{nullptr}, *balken{nullptr};
  lv_obj_t *k_zeile[4]{}, *k_tag[4]{}, *k_titel[4]{};
  int uhr_min{-1};
  // Abruf (eigener Task, Stapel im PSRAM)
  void *task{nullptr};
  std::string url;
  char *antwort{nullptr};
  volatile size_t antwort_len{0};
  volatile bool fertig{false};
  volatile bool laeuft{false};
};
inline Zustand &z() { static Zustand s; return s; }

inline std::string utf8(uint32_t cp) {
  std::string s;
  s += (char) (0xF0 | (cp >> 18));
  s += (char) (0x80 | ((cp >> 12) & 0x3F));
  s += (char) (0x80 | ((cp >> 6) & 0x3F));
  s += (char) (0x80 | (cp & 0x3F));
  return s;
}

inline uint32_t wetter_cp(const char *n) {
  static const struct { const char *n; uint32_t cp; } T[] = {
      {"weather-sunny", 0xF0599}, {"weather-night", 0xF0594}, {"weather-partly-cloudy", 0xF0595},
      {"weather-night-partly-cloudy", 0xF0F31}, {"weather-cloudy", 0xF0590}, {"weather-fog", 0xF0591},
      {"weather-hail", 0xF0592}, {"weather-lightning", 0xF0593}, {"weather-lightning-rainy", 0xF067E},
      {"weather-pouring", 0xF0596}, {"weather-rainy", 0xF0597}, {"weather-snowy", 0xF0598},
      {"weather-snowy-rainy", 0xF067F}, {"weather-windy", 0xF059D}, {"weather-windy-variant", 0xF059E},
      {"weather-sunny-alert", 0xF0F37}};
  for (const auto &e : T) if (n && !strcmp(n, e.n)) return e.cp;
  return 0xF0590;
}

// ---------------------------------------------------------------- Abruf
inline void abruf_task(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    auto &s = z();
    esp_http_client_config_t cfg = {};
    cfg.url = s.url.c_str();
    cfg.timeout_ms = 6000;
    cfg.buffer_size = 1024;
    cfg.keep_alive_enable = false;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    size_t len = 0;
    char *buf = nullptr;
    if (c != nullptr && esp_http_client_open(c, 0) == ESP_OK) {
      esp_http_client_fetch_headers(c);
      if (esp_http_client_get_status_code(c) == 200) {
        buf = (char *) heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        while (buf && len < 4095) {
          int r = esp_http_client_read(c, buf + len, 4095 - len);
          if (r <= 0) break;
          len += r;
        }
        if (buf) buf[len] = 0;
      }
    }
    if (c != nullptr) { esp_http_client_close(c); esp_http_client_cleanup(c); }
    if (s.antwort) heap_caps_free(s.antwort);
    s.antwort = buf;
    s.antwort_len = len;
    s.fertig = true;
    s.laeuft = false;
  }
}

inline void abrufen() {
  auto &s = z();
  // fertig = Ergebnis noch nicht uebernommen: erst anwenden (sonst gibt der Task den Puffer frei, waehrend er gelesen wird)
  if (s.laeuft || s.fertig || !api::global_api_server || !api::global_api_server->is_connected()) return;
  s.url = "http://" + tk_cfg->host() + "/api/esphomeremote_konfig/anzeige/" + App.get_name() + "?k=" + tk_cfg->schluessel();
  if (s.task == nullptr) {
    static StaticTask_t tcb;
    const size_t groesse = 6144;
    auto *stapel = (StackType_t *) heap_caps_malloc(groesse, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stapel == nullptr) return;
    s.task = xTaskCreateStaticPinnedToCore(abruf_task, "dock_dl", groesse, nullptr, 2, stapel, &tcb, tskNO_AFFINITY);
  }
  s.laeuft = true;
  s.abruf_ms = millis();
  xTaskNotifyGive((TaskHandle_t) s.task);
}

// ---------------------------------------------------------------- Aufbau
inline lv_obj_t *text(lv_obj_t *p, const lv_font_t *f, uint32_t farbe, const char *t = "") {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_label_set_text(l, t);
  return l;
}

// Unsichtbarer Rahmen, der sich der Groesse seines Inhalts anpasst (Reihe oder Spalte) - mit lv_obj_align
// mittig gesetzt bleibt er auch mittig, wenn sich die Texte aendern (2026-10-17: alles mittig).
inline lv_obj_t *rahmen(lv_obj_t *p, lv_flex_flow_t fluss, int abstand) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, fluss);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(o, abstand, 0);
  lv_obj_set_style_pad_row(o, abstand, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
  return o;
}

inline void aufbauen() {
  auto &s = z();
  if (s.gebaut) return;
  s.gebaut = true;
  lv_obj_t *r = dock_root;
  lv_obj_clean(r);
  s.datum = text(r, a15->get_lv_font(), 0x8B94A3);
  lv_obj_align(s.datum, LV_ALIGN_TOP_MID, 0, 12);
  s.uhr = text(r, a48->get_lv_font(), 0xF1F3F5, "--:--");
  lv_obj_align(s.uhr, LV_ALIGN_TOP_MID, 0, 34);
  // Wetter: Symbol | Temperatur ueber Beschreibung, als Ganzes mittig
  lv_obj_t *w = rahmen(r, LV_FLEX_FLOW_ROW, 10);
  lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 102);
  s.w_ico = text(w, mdi_wetter->get_lv_font(), 0xFACC15);
  lv_obj_t *wt = rahmen(w, LV_FLEX_FLOW_COLUMN, 2);
  s.w_temp = text(wt, a18->get_lv_font(), 0xF1F3F5);
  s.w_text = text(wt, a13->get_lv_font(), 0x8B94A3);
  lv_obj_set_style_max_width(s.w_text, 170, 0);
  lv_label_set_long_mode(s.w_text, LV_LABEL_LONG_DOT);
  // Raum
  lv_obj_t *rm = rahmen(r, LV_FLEX_FLOW_ROW, 6);
  lv_obj_align(rm, LV_ALIGN_TOP_MID, 0, 154);
  s.raum_ico = text(rm, mdi_klein->get_lv_font(), 0x3B82F6, utf8(0xF050F).c_str());
  s.raum = text(rm, a15->get_lv_font(), 0xF1F3F5);
  // grauer Trennstrich zwischen Raumklima und Terminen
  lv_obj_t *strich = lv_obj_create(r);
  lv_obj_remove_style_all(strich);
  lv_obj_set_size(strich, 200, 3);
  lv_obj_align(strich, LV_ALIGN_TOP_MID, 0, 181);
  lv_obj_set_style_radius(strich, 2, 0);
  lv_obj_set_style_bg_color(strich, lv_color_hex(0x5B6472), 0);
  lv_obj_set_style_bg_opa(strich, LV_OPA_COVER, 0);
  lv_obj_add_flag(strich, LV_OBJ_FLAG_EVENT_BUBBLE);
  // Termine
  s.k_box = lv_obj_create(r);
  lv_obj_remove_style_all(s.k_box);
  lv_obj_set_size(s.k_box, 220, 92);
  lv_obj_align(s.k_box, LV_ALIGN_TOP_MID, 0, 190);
  lv_obj_set_style_bg_color(s.k_box, lv_color_hex(0x1A1F27), 0);
  lv_obj_set_style_bg_opa(s.k_box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s.k_box, 10, 0);
  lv_obj_clear_flag(s.k_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s.k_box, LV_OBJ_FLAG_EVENT_BUBBLE);
  for (int i = 0; i < 3; i++) {
    s.k_tag[i] = text(s.k_box, a13->get_lv_font(), 0x3B82F6);
    lv_obj_set_pos(s.k_tag[i], 8, 6 + i * 28);
    s.k_titel[i] = text(s.k_box, a13->get_lv_font(), 0xF1F3F5);
    lv_obj_set_pos(s.k_titel[i], 8, 6 + i * 28 + 13);
    for (lv_obj_t *l : {s.k_tag[i], s.k_titel[i]}) {
      lv_obj_set_width(l, 204);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
  }
  // Laden: Symbol + Text mittig, darunter der Balken ueber die ganze Breite am unteren Rand -
  // mit sichtbarer Schiene, damit man sieht, wo "voll" ist
  lv_obj_t *ld = rahmen(r, LV_FLEX_FLOW_ROW, 6);
  lv_obj_align(ld, LV_ALIGN_TOP_MID, 0, 288);
  s.lade_ico = text(ld, mdi_klein->get_lv_font(), 0x22C55E, utf8(0xF12A6).c_str());
  s.lade = text(ld, a13->get_lv_font(), 0x22C55E);
  s.balken = lv_bar_create(r);
  lv_obj_set_size(s.balken, 240, 8);
  lv_obj_align(s.balken, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(s.balken, 0, 100);
  lv_obj_set_style_radius(s.balken, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s.balken, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s.balken, lv_color_hex(0x3A4250), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s.balken, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s.balken, lv_color_hex(0x22C55E), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s.balken, LV_OPA_COVER, LV_PART_INDICATOR);
  for (lv_obj_t *o : {s.datum, s.uhr, s.w_ico, s.w_temp, s.w_text, s.raum_ico, s.raum, s.lade_ico, s.lade, s.balken})
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
}

inline void uhr_zeigen(bool immer) {
  auto &s = z();
  if (!s.gebaut) return;
  auto t = sntp_time->now();
  if (!t.is_valid()) { lv_label_set_text(s.uhr, "--:--"); return; }
  if (!immer && t.minute == s.uhr_min) return;
  s.uhr_min = t.minute;
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", t.hour, t.minute);
  lv_label_set_text(s.uhr, b);
  static const char *TAG_N[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  static const char *MON[] = {"Jan", "Feb", "März", "Apr", "Mai", "Juni", "Juli", "Aug", "Sep", "Okt", "Nov", "Dez"};
  char d[32];
  snprintf(d, sizeof(d), "%s, %d. %s", TAG_N[(t.day_of_week + 6) % 7], t.day_of_month, MON[(t.month + 11) % 12]);
  lv_label_set_text(s.datum, d);
}

inline void laden_zeigen() {
  auto &s = z();
  if (!s.gebaut) return;
  const int pct = akku_pct->value();
  const bool kabel = charger_present->state;
  char b[40];
  if (kabel && laedt->value()) snprintf(b, sizeof(b), "Lädt · %d %%", pct);
  else if (kabel || akku_voll->value() || pct >= 99) snprintf(b, sizeof(b), "Voll · %d %%", pct);
  else snprintf(b, sizeof(b), "Akku · %d %%", pct);
  lv_label_set_text(s.lade, b);
  lv_label_set_text(s.lade_ico, utf8(kabel && laedt->value() ? 0xF12A6 : 0xF12A3).c_str());
  lv_bar_set_value(s.balken, pct, LV_ANIM_OFF);
}

inline void daten_anwenden() {
  auto &s = z();
  s.fertig = false;
  if (!s.gebaut || s.antwort == nullptr) return;
  JsonDocument doc;
  if (deserializeJson(doc, s.antwort, s.antwort_len)) { ESP_LOGW("dock", "Anzeige-Daten nicht lesbar"); return; }
  char b[64];
  JsonObjectConst w = doc["w"];
  if (!w.isNull()) {
    lv_label_set_text(s.w_ico, utf8(wetter_cp(w["i"] | "")).c_str());
    if (w["t"].isNull()) lv_label_set_text(s.w_temp, "");
    else { snprintf(b, sizeof(b), "%d°", (int) (w["t"] | 0)); lv_label_set_text(s.w_temp, b); }
    std::string t = w["z"] | "";
    if (!w["h"].isNull() && !w["l"].isNull()) {
      snprintf(b, sizeof(b), " · %d° / %d°", (int) (w["h"] | 0), (int) (w["l"] | 0));
      t += b;
    }
    lv_label_set_text(s.w_text, t.c_str());
  }
  JsonObjectConst r = doc["r"];
  if (!r.isNull()) {
    std::string t;
    if (!r["t"].isNull()) { snprintf(b, sizeof(b), "%.1f °C", (float) (r["t"] | 0.0f)); t = b; for (auto &c : t) if (c == '.') c = ','; }
    if (!r["f"].isNull()) { snprintf(b, sizeof(b), "%s%d %%", t.empty() ? "" : "   ", (int) (r["f"] | 0)); t += b; }
    lv_label_set_text(s.raum, t.c_str());
  }
  JsonArrayConst k = doc["k"];
  int n = 0;
  for (JsonArrayConst e : k) {
    if (n >= 3) break;
    snprintf(b, sizeof(b), "%s  %s", (const char *) (e[0] | ""), (const char *) (e[1] | ""));
    lv_label_set_text(s.k_tag[n], b);
    lv_label_set_text(s.k_titel[n], e[2] | "");
    n++;
  }
  for (int i = n; i < 3; i++) { lv_label_set_text(s.k_tag[i], i == 0 ? "Keine Termine" : ""); lv_label_set_text(s.k_titel[i], ""); }
  lv_obj_set_height(s.k_box, n == 0 ? 26 : 8 + n * 28);
}

// ---------------------------------------------------------------- Ablauf
inline bool im_dock() { return z().im_dock; }

inline bool display_soll_an() {
  const int m = dock_disp->value();
  if (m == 1) return true;
  if (m != 2) return false;
  auto t = sntp_time->now();
  if (!t.is_valid()) return true;
  const int von = dock_von->value(), bis = dock_bis->value();
  return von <= bis ? (t.hour >= von && t.hour < bis) : (t.hour >= von || t.hour < bis);
}

inline void zeigen() {
  if (z().im_dock && musik::im_dock_zeigen()) { musik::zeigen(true); return; }
  aufbauen();
  uhr_zeigen(true);
  laden_zeigen();
  tasten::seite_zeigen(page_dock, LV_SCR_LOAD_ANIM_FADE_IN);
  if (button_backlight->current_values.is_on()) button_backlight->turn_off().perform();
  if (millis() - z().abruf_ms > 50000 || z().abruf_ms == 0) abrufen();
}

inline void verlassen(const char *grund) {
  auto &s = z();
  if (!s.im_dock) return;
  s.im_dock = false;
  s.lage_ok = false;
  ESP_LOGI("dock", "Aus dem Dock (%s)", grund);
  if (cur_page->value() == 50) { go_home->execute(); wake_backlight->execute(); }
}

// Seite geladen (on_load)
inline void geladen() {
  aufbauen();
  uhr_zeigen(true);
  laden_zeigen();
}

// Tipp auf die Dock-Anzeige: Fernbedienung zeigen
inline void beruehrt() {
  last_input_ms->value() = millis();
  go_home->execute();
}

inline void dock_takt();
inline void seite_takt();
// Alle 250 ms
inline void takt() {
  seite_takt();
  dock_takt();
  musik::dock_takt(z().im_dock);
}

inline void dock_takt() {
  auto &s = z();
  const uint32_t jetzt = millis();
  const bool kabel = charger_present->state;
  if (kabel && !s.kabel_vorher) s.kabel_seit = jetzt;
  s.kabel_vorher = kabel;
  if (!dock_an->value()) { if (s.im_dock) verlassen("ausgeschaltet"); return; }
  if (!kabel && !s.im_dock) return;   // ohne Kabel und nicht im Dock: nichts zu tun (kein I2C)
  float x = 0, y = 0, zz = 0;
  const bool lage = lage_lesen(x, y, zz);
  if (!s.im_dock) {
    // Hineingestellt: Kabel seit 1,5 s da, Lage ruhig -> Dock-Lage merken
    if (kabel && jetzt - s.kabel_seit > 1500 && lage) {
      s.im_dock = true;
      s.lx = x; s.ly = y; s.lz = zz;
      s.lage_ok = true;
      s.weg = 0;
      ESP_LOGI("dock", "Im Dock");
      if (!disp_off->value() || display_soll_an()) zeigen();
    }
    return;
  }
  // Im Dock: herausgenommen?
  if (lage && s.lage_ok) {
    const float deg = remote_strom::winkel_grad(x, y, zz, s.lx, s.ly, s.lz);
    if (deg >= (kabel ? 25.0f : 12.0f)) { if (++s.weg >= 2) { verlassen("Lage"); return; } }
    else s.weg = 0;
  } else if (!kabel && !lage) {
    // ohne Lage (Sensor aus) und ohne Kabel: nicht laenger im Dock annehmen
    verlassen("kein Kabel");
    return;
  }
  // Display wach halten
  if (display_soll_an() && jetzt - s.wach_ms > 5000) {
    s.wach_ms = jetzt;
    if (disp_off->value()) wake_backlight->execute();
    else idle_touch_ping->execute();
  }
  // Zurueck zur Dock-Anzeige: von der Startseite, 20 s nach der letzten Eingabe, kein Popup
  const int p = cur_page->value();
  if (p == 0 && !disp_off->value() && popup_seite->value() < 0 && jetzt - last_input_ms->value() > 20000) zeigen();
  if (p == 50 && jetzt - last_input_ms->value() < 400) go_home->execute();   // Taste im Dock: Fernbedienung
}

// Sichtbare Seite aktuell halten (auch wenn sie ueber Menue/Taste geoeffnet wurde)
inline void seite_takt() {
  auto &s = z();
  if (s.fertig) daten_anwenden();
  if (cur_page->value() != 50) return;
  const uint32_t jetzt = millis();
  uhr_zeigen(false);
  static uint32_t lade_ms = 0;
  if (jetzt - lade_ms > 5000) { lade_ms = jetzt; laden_zeigen(); }
  if (jetzt - s.abruf_ms > 60000 || s.abruf_ms == 0) abrufen();
}

}  // namespace dockanz

// fuer andere Module (Gesten: nicht im Dock)
inline bool dockanz_im_dock() { return dockanz::im_dock(); }
