#pragma once
// Dock-Anzeige / "Smart Display" (2026-10-17): Steht die Remote im Dock, zeigt sie statt der Fernbedienung
// Uhrzeit, Wetter, Raumklima, die naechsten Termine und den Ladestand (Seite page_dock, cur_page 50).
//
// Im Dock?  Das Ladekabel (Pogo-Pins) meldet sich -> die Lage (Bewegungssensor) wird als "Dock-Lage" gemerkt.
// Solange sie bleibt, gilt die Remote als im Dock - auch wenn der Lade-IC bei vollem Akku "nicht mehr laden"
// meldet. Weicht die Lage deutlich ab (herausgenommen), sofort zurueck zur Fernbedienung.
// Taste oder Tipp im Dock: die Fernbedienung erscheint; wenn das Display sonst ausginge, wieder die Dock-Anzeige.
// Display im Dock (Einstellung dock_disp): 0 = wie sonst (geht nach der Zeit aus), 1 = bleibt an,
// 2 = bleibt an zwischen dock_von und dock_bis Uhr. Helligkeit wie immer (Lichtsensor-Kurve).
// Daten: GET /api/esphomeremote/anzeige/<geraet> (HA, anzeige.py), jede Minute solange sichtbar.

#include <esp_http_client.h>

// Musik-Anzeige (esphomeremote-music.h, danach eingebunden): im Dock statt des Smart Displays, solange Musik laeuft
namespace musik {
inline bool im_dock_zeigen();
inline void zeigen(bool vom_dock);
inline void dock_takt(bool im_dock);
inline bool player_spielt();
inline void titel_anwenden();
inline void ausgewaehlt_verlassen();
}

namespace dockanz {

struct Zustand {
  bool im_dock{false};
  uint32_t ohne_strom_ms{0};   // seit wann im Dock ohne Ladestrom bei nicht vollem Akku (0 = nicht)
  float lx{0}, ly{0}, lz{0};
  bool lage_ok{false};
  int weg{0};
  uint32_t kabel_seit{0};
  uint32_t kabel_weg_seit{0};
  uint32_t gezeigt_ms{0};       // zuletzt auf Dock-/Musik-Anzeige umgeschaltet (cur_page wechselt erst nach dem Einblenden)   // seit wann die Ladekontakte keinen Strom mehr haben
  bool kabel_vorher{false};
  uint32_t wach_ms{0};
  uint32_t abruf_ms{0};
  bool gebaut{false};
  lv_obj_t *datum{nullptr}, *uhr{nullptr}, *w_ico{nullptr}, *w_temp{nullptr}, *w_text{nullptr}, *raum{nullptr},
      *raum_ico{nullptr}, *k_box{nullptr}, *lade{nullptr}, *lade_ico{nullptr}, *balken{nullptr};
  lv_obj_t *k_zeile[4]{}, *k_tag[4]{}, *k_titel[4]{};
  int uhr_min{-1};
  // Steuerleiste und Wahl (2026-10-31, Nutzer: Musik/Uhr umschalten, "Heute aus", Tipp nicht gleich weg)
  int wahl{0};                  // 0 automatisch, 1 Uhr, 2 Musik - zurueck auf 0, wenn Musik anfaengt/aufhoert
  bool spielte{false};
  bool pausiert{false};         // "Heute aus": bis zum naechsten Herausnehmen kein Smart Display
  lv_obj_t *leiste{nullptr};
  uint32_t leiste_bis{0};
  uint32_t geste_ms{0};
  bool geste_an[2]{false, false};
  // Ansicht aus dem Konfigurator (Daten "a"): Teile in Reihenfolge, Musik mit Titel, Antippen = Steuerleiste
  std::vector<std::string> teile{"datum", "uhr", "wetter", "raum", "termine", "laden"};
  bool musik_titel{true};
  int musik_stil{1};            // 0 nur Spektrum, 1 mit Titel, 2 Cover im Hintergrund mit Titel
  bool tipp_leiste{true};
  lv_obj_t *c_wetter{nullptr}, *c_raum{nullptr}, *c_termine{nullptr}, *c_laden{nullptr};
  // Abruf (eigener Task, Stapel im PSRAM)
  void *task{nullptr};
  std::string url;
  char *antwort{nullptr};
  volatile size_t antwort_len{0};
  volatile bool fertig{false};
  volatile bool laeuft{false};
};
inline Zustand &z() { static Zustand s; return s; }
inline void leiste_weg();   // Steuerleiste (weiter unten)
inline void geste_anmelden(lv_obj_t *root, int i);

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
  // Ohne eigene HA-Verbindung ueber das Dock (2026-10-31: im Dock-Betrieb kamen sonst nie Wetter/Termine)
  const bool api = api::global_api_server && api::global_api_server->is_connected();
  if (s.laeuft || s.fertig || (!api && !(tk_cfg->laden_ueber_dock && tk_cfg->laden_ueber_dock()))) return;
  s.url = "http://" + tk_cfg->host() + "/api/esphomeremote/anzeige/" + App.get_name() + "?k=" + tk_cfg->schluessel();
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
  lv_obj_t *w = s.c_wetter = rahmen(r, LV_FLEX_FLOW_ROW, 10);
  lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 102);
  s.w_ico = text(w, mdi_wetter->get_lv_font(), 0xFACC15);
  lv_obj_t *wt = rahmen(w, LV_FLEX_FLOW_COLUMN, 2);
  s.w_temp = text(wt, a18->get_lv_font(), 0xF1F3F5);
  s.w_text = text(wt, a13->get_lv_font(), 0x8B94A3);
  lv_obj_set_style_max_width(s.w_text, 170, 0);
  lv_label_set_long_mode(s.w_text, LV_LABEL_LONG_DOT);
  // Raum
  lv_obj_t *rm = s.c_raum = rahmen(r, LV_FLEX_FLOW_ROW, 6);
  lv_obj_align(rm, LV_ALIGN_TOP_MID, 0, 154);
  s.raum_ico = text(rm, mdi_klein->get_lv_font(), akzent::farbe(), utf8(0xF050F).c_str());
  s.raum = text(rm, a15->get_lv_font(), 0xF1F3F5);
  // grauer Trennstrich ueber den Terminen (Block "termine" = Strich + Kasten, 2026-10-31)
  s.c_termine = rahmen(r, LV_FLEX_FLOW_COLUMN, 6);
  lv_obj_t *strich = lv_obj_create(s.c_termine);
  lv_obj_remove_style_all(strich);
  lv_obj_set_size(strich, 200, 3);

  lv_obj_set_style_radius(strich, 2, 0);
  lv_obj_set_style_bg_color(strich, lv_color_hex(0x5B6472), 0);
  lv_obj_set_style_bg_opa(strich, LV_OPA_COVER, 0);
  lv_obj_add_flag(strich, LV_OBJ_FLAG_EVENT_BUBBLE);
  // Termine
  s.k_box = lv_obj_create(s.c_termine);
  lv_obj_remove_style_all(s.k_box);
  lv_obj_set_size(s.k_box, 220, 92);

  lv_obj_set_style_bg_color(s.k_box, lv_color_hex(0x1A1F27), 0);
  lv_obj_set_style_bg_opa(s.k_box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(s.k_box, 10, 0);
  lv_obj_clear_flag(s.k_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s.k_box, LV_OBJ_FLAG_EVENT_BUBBLE);
  for (int i = 0; i < 3; i++) {
    s.k_tag[i] = text(s.k_box, a13->get_lv_font(), akzent::farbe());
    lv_obj_set_pos(s.k_tag[i], 8, 6 + i * 28);
    s.k_titel[i] = text(s.k_box, a13->get_lv_font(), 0xF1F3F5);
    lv_obj_set_pos(s.k_titel[i], 8, 6 + i * 28 + 13);
    for (lv_obj_t *l : {s.k_tag[i], s.k_titel[i]}) {
      lv_obj_set_width(l, 204);
      lv_obj_set_height(l, 16);   // eine Zeile - lange Titel enden mit "..." statt in den naechsten Termin zu laufen (2026-11-01)
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
  }
  // Laden: Symbol + Text mittig, darunter der Balken ueber die ganze Breite am unteren Rand -
  // mit sichtbarer Schiene, damit man sieht, wo "voll" ist
  lv_obj_t *ld = s.c_laden = rahmen(r, LV_FLEX_FLOW_ROW, 6);
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

// Teile in der gewaehlten Reihenfolge untereinander, das Ganze senkrecht mittig (2026-10-31). Ausgeblendetes faellt weg.
inline void anordnen() {
  auto &s = z();
  if (!s.gebaut) return;
  struct B { const char *n; lv_obj_t *o; int luecke; };
  const B alle[] = {{"datum", s.datum, 4}, {"uhr", s.uhr, 8}, {"wetter", s.c_wetter, 10}, {"raum", s.c_raum, 8},
                    {"termine", s.c_termine, 8}, {"laden", s.c_laden, 0}};
  std::vector<const B *> zeigen;
  for (const auto &t : s.teile)
    for (const auto &b : alle) if (t == b.n) zeigen.push_back(&b);
  for (const auto &b : alle) lv_obj_add_flag(b.o, LV_OBJ_FLAG_HIDDEN);
  for (const B *b : zeigen) lv_obj_remove_flag(b->o, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(dock_root);
  const bool balken = std::any_of(zeigen.begin(), zeigen.end(), [](const B *b) { return !strcmp(b->n, "laden"); });
  if (balken) lv_obj_remove_flag(s.balken, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s.balken, LV_OBJ_FLAG_HIDDEN);
  int gesamt = 0;
  for (size_t i = 0; i < zeigen.size(); i++) gesamt += lv_obj_get_height(zeigen[i]->o) + (i + 1 < zeigen.size() ? zeigen[i]->luecke : 0);
  const int frei = 320 - (balken ? 10 : 0);
  int y = std::max(8, (frei - gesamt) / 2);
  if (gesamt > frei - 40) y = std::min(y, 12);   // fast voll: oben anfangen wie bisher, sonst mittig (2026-11-01)
  for (const B *b : zeigen) {
    lv_obj_align(b->o, LV_ALIGN_TOP_MID, 0, y);
    y += lv_obj_get_height(b->o) + b->luecke;
  }
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
  snprintf(d, sizeof(d), sprache::englisch() ? "%s, %d %s" : "%s, %d. %s", sprache::tr(TAG_N[(t.day_of_week + 6) % 7]), t.day_of_month,
           sprache::tr(MON[(t.month + 11) % 12]));
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
  // Ansicht aus dem Konfigurator
  JsonObjectConst a = doc["a"];
  if (!a.isNull()) {
    std::vector<std::string> t;
    for (JsonVariantConst x : a["t"].as<JsonArrayConst>()) t.emplace_back(x | "");
    s.teile = t;
    s.musik_stil = a["m"] | 1;
    s.musik_titel = s.musik_stil != 0;
    s.tipp_leiste = (a["p"] | 1) != 0;
    musik::titel_anwenden();
  }
  anordnen();
}

// ---------------------------------------------------------------- Ablauf
inline bool im_dock() { return z().im_dock; }
// Dock- oder Musik-Anzeige sichtbar, im Dock und Display soll an bleiben: dann nie abdunkeln (backlight_idle_timeout).
// Auf den Fernbedienungsseiten dunkelt es normal ab; statt auszugehen kommt die Anzeige in voller Helligkeit.
inline bool display_soll_an();
inline bool anzeige_haelt_an() {
  const int p = cur_page->value();
  return z().im_dock && !z().pausiert && (p == 50 || p == 52 || millis() - z().gezeigt_ms < 2000) && display_soll_an();
}
// Start des Display-Ruhe-Timers (backlight_idle_timeout setzt ihn) - daraus: wann das Display ausginge
inline uint32_t &ruhe_start_ms() { static uint32_t t = 0; return t; }

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
  z().gezeigt_ms = millis();
  if (z().im_dock && musik::im_dock_zeigen()) { musik::zeigen(true); return; }
  aufbauen();
  anordnen();
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
  s.pausiert = false;
  s.wahl = 0;
  leiste_weg();
  ESP_LOGI("dock", "Aus dem Dock (%s)", grund);
  if (cur_page->value() == 50) { go_home->execute(); wake_backlight->execute(); }
}

// Seite geladen (on_load)
inline void geladen() {
  aufbauen();
  anordnen();
  uhr_zeigen(true);
  laden_zeigen();
  geste_anmelden(dock_root, 0);
}

// ---------------------------------------------------------------- Steuerleiste (2026-10-31)
// Tipp auf Smart Display oder Musik-Anzeige: unten eine Leiste fuer 5 s, Inhalt je nach Ansicht:
//   Uhr:   [Remote] [Musik] [Heute aus]          Musik: [<<] [>||] [>>] [Uhr] [Remote]
// Wischen links/rechts = Uhr <-> Musik, nach unten = Fernbedienung. Tasten wirken wie immer (-> Fernbedienung).
enum Aktion : intptr_t { A_REMOTE = 1, A_MUSIK, A_UHR, A_AUS, A_ZURUECK, A_PLAY, A_VOR };

inline void leiste_weg() {
  auto &s = z();
  if (s.leiste) lv_obj_add_flag(s.leiste, LV_OBJ_FLAG_HIDDEN);
  s.leiste_bis = 0;
}

inline void zur_fernbedienung() {
  leiste_weg();
  musik::ausgewaehlt_verlassen();
  last_input_ms->value() = millis();
  go_home->execute();
}

inline void zur_uhr();
inline void zur_musik();
inline void heute_aus();
inline void leiste_zeigen();

inline void leiste_klick(lv_event_t *e) {
  const intptr_t a = (intptr_t) lv_event_get_user_data(e);
  auto &s = z();
  s.leiste_bis = millis() + 5000;
  last_input_ms->value() = millis();
  switch (a) {
    case A_REMOTE: zur_fernbedienung(); break;
    case A_MUSIK: zur_musik(); break;
    case A_UHR: zur_uhr(); break;
    case A_AUS: heute_aus(); break;
    case A_ZURUECK: mp_taste->execute(2); break;
    case A_PLAY: mp_taste->execute(0); break;
    case A_VOR: mp_taste->execute(1); break;
  }
}

inline void knopf(lv_obj_t *p, const char *ico, const char *t, Aktion a, int breite) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_size(b, breite, 50);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x232A35), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(akzent::stufen(akzent::farbe()).f[1]), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b, 1, 0);
  text(b, ico_leiste->get_lv_font(), 0xF1F3F5, ico);
  if (t && *t) text(b, a11->get_lv_font(), 0x8B94A3, t);
  lv_obj_add_event_cb(b, leiste_klick, LV_EVENT_CLICKED, (void *) (intptr_t) a);
}

inline void leiste_zeigen() {
  auto &s = z();
  if (s.leiste == nullptr) {
    s.leiste = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.leiste);
    lv_obj_set_size(s.leiste, 228, 62);
    lv_obj_align(s.leiste, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(s.leiste, lv_color_hex(0x111418), 0);
    lv_obj_set_style_bg_opa(s.leiste, 240, 0);
    lv_obj_set_style_radius(s.leiste, 16, 0);
    lv_obj_set_style_border_width(s.leiste, 2, 0);
    lv_obj_set_style_border_color(s.leiste, lv_color_hex(0x3A424E), 0);
    lv_obj_set_style_pad_all(s.leiste, 4, 0);
    lv_obj_set_flex_flow(s.leiste, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s.leiste, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s.leiste, LV_OBJ_FLAG_SCROLLABLE);
  }
  lv_obj_clean(s.leiste);
  if (cur_page->value() == 52) {
    knopf(s.leiste, utf8(0xF04AE).c_str(), "", A_ZURUECK, 36);
    knopf(s.leiste, utf8(0xF040E).c_str(), "", A_PLAY, 36);
    knopf(s.leiste, utf8(0xF04AD).c_str(), "", A_VOR, 36);
    knopf(s.leiste, utf8(0xF0150).c_str(), "Uhr", A_UHR, 50);
    knopf(s.leiste, utf8(0xF0454).c_str(), "Remote", A_REMOTE, 56);
  } else {
    knopf(s.leiste, utf8(0xF0454).c_str(), "Remote", A_REMOTE, 64);
    if (mic_allowed->value()) knopf(s.leiste, utf8(0xF075A).c_str(), musik::player_spielt() ? "Musik" : "Spektrum", A_MUSIK, 72);
    if (s.im_dock) knopf(s.leiste, utf8(0xF04B2).c_str(), "Heute aus", A_AUS, 72);
  }
  lv_obj_remove_flag(s.leiste, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s.leiste);
  s.leiste_bis = millis() + 5000;
}

// Leiste neu aufbauen, wenn die neue Seite steht (2026-11-01: nach "Musik"/"Uhr" zeigte sie noch die Knoepfe der alten
// Ansicht - cur_page wechselt erst nach dem Einblenden)
inline void leiste_spaeter() {
  z().leiste_bis = millis() + 5000;   // takt blendet sie in der Zwischenzeit nicht aus
  lv_timer_t *t = lv_timer_create([](lv_timer_t *t) { lv_timer_delete(t); const int p = cur_page->value(); if (p == 50 || p == 52) leiste_zeigen(); }, 450, nullptr);
  (void) t;
}

// Tipp auf die Dock-/Musik-Anzeige: Steuerleiste (Einstellung "Antippen") oder wie frueher gleich die Fernbedienung
inline void beruehrt() {
  auto &s = z();
  last_input_ms->value() = millis();
  if (millis() - s.geste_ms < 500) return;   // Wischen loest auch einen Klick aus
  if (!s.tipp_leiste) { zur_fernbedienung(); return; }
  if (s.leiste && !lv_obj_has_flag(s.leiste, LV_OBJ_FLAG_HIDDEN)) leiste_weg();
  else leiste_zeigen();
}

// Wischen auf der Anzeige: links/rechts Uhr <-> Musik, nach unten Fernbedienung
inline void geste(lv_event_t *) {
  auto &s = z();
  lv_indev_t *in = lv_indev_active();
  if (in == nullptr) return;
  const lv_dir_t d = lv_indev_get_gesture_dir(in);
  s.geste_ms = millis();
  last_input_ms->value() = millis();
  if (d == LV_DIR_BOTTOM) zur_fernbedienung();
  else if (d == LV_DIR_LEFT || d == LV_DIR_RIGHT) {
    if (cur_page->value() == 52) zur_uhr();
    else if (mic_allowed->value()) zur_musik();
  }
}
inline void geste_anmelden(lv_obj_t *root, int i) {
  auto &s = z();
  if (s.geste_an[i]) return;
  s.geste_an[i] = true;
  lv_obj_clear_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);   // Geste hier auswerten, nicht beim Bildschirm
  lv_obj_add_event_cb(root, geste, LV_EVENT_GESTURE, nullptr);
}

inline void dock_takt();
inline void seite_takt();
// Alle 250 ms
inline void takt() {
  seite_takt();
  dock_takt();
  musik::dock_takt(z().im_dock);
  auto &s = z();
  const int p = cur_page->value();
  if (s.leiste_bis != 0 && ((int32_t) (millis() - s.leiste_bis) >= 0 || ((p != 50 && p != 52) && millis() - s.gezeigt_ms > 1500) || disp_off->value())) leiste_weg();
}

// Aktionen der Steuerleiste
inline void zur_uhr() {
  auto &s = z();
  s.wahl = 1;
  musik::ausgewaehlt_verlassen();
  zeigen();
  if (s.leiste_bis) leiste_spaeter();
}
inline void zur_musik() {
  auto &s = z();
  s.wahl = 2;
  s.gezeigt_ms = millis();
  musik::zeigen(s.im_dock);
  if (s.leiste_bis) leiste_spaeter();
}
inline void heute_aus() {
  auto &s = z();
  s.pausiert = true;
  ESP_LOGI("dock", "Smart Display: heute aus (bis zum Herausnehmen)");
  zur_fernbedienung();
}
// Konfigurator "Auf der Remote zeigen" (auch ausserhalb des Docks; Tipp -> Leiste -> Remote)
inline void vorschau() {
  wake_backlight->execute();
  zeigen();
  abrufen();
}
// Pruefen per Bildschirmfoto (Dienst menue_popup "_smartdisplay_leiste" / "_musik_leiste"): Ansicht mit Steuerleiste
inline void vorschau_leiste(bool musik) {
  wake_backlight->execute();
  if (musik) { z().wahl = 2; z().gezeigt_ms = millis(); musik::zeigen(z().im_dock); } else zeigen();
  abrufen();
  leiste_spaeter();
}

inline void dock_takt() {
  auto &s = z();
  const uint32_t jetzt = millis();
  const bool kabel = charger_present->state;
  if (kabel && !s.kabel_vorher) s.kabel_seit = jetzt;
  if (!kabel && s.kabel_vorher) s.kabel_weg_seit = jetzt;
  s.kabel_vorher = kabel;
  if (!dock_an->value()) { if (s.im_dock) verlassen("ausgeschaltet"); return; }
  if (!kabel && !s.im_dock) return;   // ohne Kabel und nicht im Dock: nichts zu tun (kein I2C)
  float x = 0, y = 0, zz = 0;
  const bool lage = lage_lesen(x, y, zz);
  if (!s.im_dock) {
    // Hineingestellt: Kabel seit 1,5 s da, Lage ruhig -> Dock-Lage merken
    if (kabel && jetzt - s.kabel_seit > 1500 && lage) {
      s.im_dock = true;
      s.pausiert = false;
      s.wahl = 0;
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
    if (kabel) {
      // An den Ladekontakten kann sie nicht heraus sein (2026-10-20): Bewegen im Dock ist keine Entnahme - vorher ab
      // 25 Grad kurz "heraus" (Startseite mit Bewegungs-Symbol), gleich danach "wieder drin" -> Anzeige flackerte.
      // Die neue Lage gilt als Dock-Lage; beim Herausnehmen faellt das Kabel weg, dann zaehlt der Winkel.
      if (deg >= 6.0f) { s.lx = x; s.ly = y; s.lz = zz; }
      s.weg = 0;
    } else if (jetzt - s.kabel_weg_seit < 1500) {
      // Kontakte gerade erst weg: bei kraeftigem Bewegen im Dock federn sie kurz ab (2026-10-20, Flackern blieb) -
      // erst wenn der Strom 1,5 s fehlt, entscheidet der Winkel
      s.weg = 0;
    } else if (deg >= 12.0f) { if (++s.weg >= 2) { verlassen("Lage"); return; } }
    else s.weg = 0;
  } else if (!kabel && !lage) {
    // ohne Lage (Sensor aus) und ohne Kabel: nicht laenger im Dock annehmen
    verlassen("kein Kabel");
    return;
  }
  // Dock ohne Strom (Smartplug aus, Stromausfall): die Lage bleibt, aber es laedt nicht. Bei vollem Akku meldet der
  // Ladechip ebenfalls "laedt nicht" - er laedt aber unter ~4,05 V von selbst nach. Also: kein Kabel UND Zelle
  // deutlich darunter, 2 min lang -> das Dock liefert nichts, Dock-Anzeige beenden (sonst haelt sie das Display auf
  // Akku dauerhaft an und die Remote schlaeft nie, 2026-10-19).
  if (!kabel && akku_spannung->has_state() && akku_spannung->state < 3.95f) {
    if (s.ohne_strom_ms == 0) s.ohne_strom_ms = jetzt | 1;
    else if (jetzt - s.ohne_strom_ms > 120000) { s.ohne_strom_ms = 0; verlassen("Dock ohne Strom"); return; }
  } else {
    s.ohne_strom_ms = 0;
  }
  // Musik faengt an oder hoert auf: die Wahl (Uhr/Musik) gilt nur bis dahin
  const bool spielt = musik::player_spielt();
  if (spielt != s.spielte) { s.spielte = spielt; s.wahl = 0; }
  // "Heute aus": bis zum Herausnehmen nichts halten und nicht zurueckholen
  if (s.pausiert) return;
  const int p = cur_page->value();
  const bool anzeige = p == 50 || p == 52;   // Dock- oder Musik-Anzeige sichtbar
  // Display wach halten - nur die Dock-/Musik-Anzeige. Auf der Fernbedienung laeuft die normale Display-Zeit, damit
  // die Uebernahme unten weiss, wann das Display ausginge.
  if (display_soll_an() && jetzt - s.wach_ms > 5000) {
    s.wach_ms = jetzt;
    if (disp_off->value()) { wake_backlight->execute(); zeigen(); }
    else if (anzeige) idle_touch_ping->execute();
  }
  // Zurueck zur Dock-/Musik-Anzeige erst dann, wenn das Display sonst ausginge, und statt es auszuschalten (Nutzer
  // 2026-10-20; vorher fest 20 s nach der letzten Eingabe, nur von der Startseite). Danach gilt die Einstellung
  // "Display im Dock": bleibt an bzw. geht nach der normalen Zeit aus. Nicht bei Popups und bewusstem Ausschalten.
  if (!anzeige && !disp_off->value() && popup_seite->value() < 0 && !sofort_aus->value() &&
      backlight_idle_timeout->is_running() &&
      jetzt - ruhe_start_ms() + 400 >= remote_strom::eff_disp_s(disp_timeout_s->value(), low_bat->value()) * 1000u) {
    zeigen();
    idle_touch_ping->execute();
  }
  // Taste im Dock: Fernbedienung (nur Tasten - ein Tipp holt die Steuerleiste, 2026-10-31)
  if (p == 50 && jetzt - last_key_ms->value() < 400) { leiste_weg(); go_home->execute(); }
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
