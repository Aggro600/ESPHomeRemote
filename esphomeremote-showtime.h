#pragma once
// Showtime (2026-11-26, Seite page_showtime, cur_page 53): YouTube-Listen mit Vorschaubildern, Abspielen in SmartTube
// am Streamer. Daten und Bild kommen von HA (custom_components/esphomeremote/showtime.py):
//   GET /api/esphomeremote/showtime/<geraet>?k=..&kat=..&s=..[&tipp=i]  -> Kategorien, 4 Eintraege, Bild-Pfad, Meldung
//   Bild: ein JPEG 112x280 mit den 4 Vorschaubildern untereinander (online_image showtime_bild) - pro Seite ein Download.
// Bedienung: hoch/runter = Eintrag (am Rand blaettern), links/rechts = Kategorie, OK/Antippen = abspielen bzw. Playlist
// oeffnen, Zurueck = aus der Playlist zurueck bzw. Startseite. Wischen: links/rechts Kategorie, hoch/runter Seite.
// Abgerufen wird wie bei der Dock-Anzeige ueber die eigene HA-Verbindung oder durchs Dock (HTTP-Tunnel).

#include <string>
#include <vector>

namespace showtime {

static const int ZEILEN = 4, KOPF = 32, ZH = 70, BB = 112, BH = 63;

struct Eintrag { std::string t, z, d; bool liste{false}; };

struct Zustand {
  bool gebaut{false};
  // Abruf (Task schreibt, takt liest)
  void *task{nullptr};
  std::string url;
  char *antwort{nullptr};
  size_t antwort_len{0};
  volatile bool laeuft{false}, fertig{false};
  uint32_t abruf_ms{0};
  // Stand
  std::vector<std::pair<std::string, std::string>> kats;
  std::string kat{"empfohlen"}, kat_name, vor_liste{"playlists"};
  int s{0}, n{1}, sel{0};
  bool mehr{false}, angemeldet{true};
  std::vector<Eintrag> v;
  std::string bild;            // zuletzt geladener Bild-Pfad
  uint32_t geladen_ms{0}, meldung_bis{0};
  // Anzeige
  lv_obj_t *kopf{nullptr}, *seite{nullptr}, *bildobj{nullptr}, *meldung{nullptr};
  lv_obj_t *zeile[ZEILEN]{}, *titel[ZEILEN]{}, *unter[ZEILEN]{}, *dauer[ZEILEN]{};
};

inline Zustand &z() {
  static Zustand s;
  return s;
}

// ---------------------------------------------------------------- Abruf (wie dockanz::abruf_task)
inline void abruf_task(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    auto &s = z();
    esp_http_client_config_t cfg = {};
    cfg.url = s.url.c_str();
    cfg.timeout_ms = 15000;   // HA holt die Liste evtl. erst bei YouTube
    cfg.buffer_size = 1024;
    cfg.keep_alive_enable = false;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    size_t len = 0;
    char *buf = nullptr;
    if (c != nullptr && esp_http_client_open(c, 0) == ESP_OK) {
      esp_http_client_fetch_headers(c);
      if (esp_http_client_get_status_code(c) == 200) {
        buf = (char *) heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
        while (buf && len < 8191) {
          int r = esp_http_client_read(c, buf + len, 8191 - len);
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

inline std::string kodiert(const std::string &t) {
  std::string o;
  char b[4];
  for (unsigned char ch : t) {
    if (isalnum(ch) || ch == '_' || ch == '-' || ch == '.') o += (char) ch;
    else { snprintf(b, sizeof(b), "%%%02X", ch); o += b; }
  }
  return o;
}

inline void meldung(const std::string &t, uint32_t ms = 3000) {
  auto &s = z();
  if (!s.meldung) return;
  (lv_label_set_text)(s.meldung, t.c_str());
  lv_obj_remove_flag(s.meldung, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s.meldung);
  s.meldung_bis = ms ? millis() + ms : 0;
}

inline bool erreichbar() {
  const bool api = api::global_api_server && api::global_api_server->is_connected();
  return api || (tk_cfg->laden_ueber_dock && tk_cfg->laden_ueber_dock());
}

inline void abrufen(int tipp = -1) {
  auto &s = z();
  if (s.laeuft || s.fertig) return;
  if (!erreichbar()) { meldung(sprache::englisch() ? "No connection to Home Assistant" : "Keine Verbindung zu Home Assistant"); return; }
  s.url = "http://" + tk_cfg->host() + "/api/esphomeremote/showtime/" + App.get_name() + "?k=" + tk_cfg->schluessel() +
          "&kat=" + kodiert(s.kat) + "&s=" + std::to_string(s.s) + (tipp >= 0 ? "&tipp=" + std::to_string(tipp) : "");
  if (s.task == nullptr) {
    static StaticTask_t tcb;
    const size_t groesse = 6144;
    auto *stapel = (StackType_t *) heap_caps_malloc(groesse, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stapel == nullptr) return;
    s.task = xTaskCreateStaticPinnedToCore(abruf_task, "showtime_dl", groesse, nullptr, 2, stapel, &tcb, tskNO_AFFINITY);
  }
  s.laeuft = true;
  s.abruf_ms = millis();
  if (tipp < 0) meldung(sprache::englisch() ? "Loading …" : "Lade …", 0);
  xTaskNotifyGive((TaskHandle_t) s.task);
}

// ---------------------------------------------------------------- Aufbau
inline lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t farbe) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_label_set_text(l, "");
  lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

inline void tippen(int i);
inline void geste(lv_event_t *e);

inline void aufbauen() {
  auto &s = z();
  if (s.gebaut) return;
  s.gebaut = true;
  lv_obj_t *r = showtime_root;
  lv_obj_set_style_bg_color(r, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(r, geste, LV_EVENT_GESTURE, nullptr);
  // Kopf: "‹ Kategorie ›" und Seite
  s.kopf = label(r, a15->get_lv_font(), 0xFFFFFF);
  lv_obj_set_width(s.kopf, 180);
  lv_label_set_long_mode(s.kopf, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s.kopf, 6, 7);
  s.seite = label(r, a10->get_lv_font(), 0x9E9E9E);
  lv_obj_set_width(s.seite, 50);
  lv_obj_set_style_text_align(s.seite, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(s.seite, 186, 10);
  lv_obj_t *strich = lv_obj_create(r);
  lv_obj_set_size(strich, 240, 2);
  lv_obj_set_pos(strich, 0, KOPF - 3);
  lv_obj_set_style_bg_color(strich, lv_color_hex(0xE53935), 0);
  lv_obj_set_style_border_width(strich, 0, 0);
  lv_obj_set_style_radius(strich, 0, 0);
  lv_obj_remove_flag(strich, LV_OBJ_FLAG_CLICKABLE);
  // Sammelbild der 4 Vorschaubilder
  s.bildobj = lv_image_create(r);
  lv_obj_set_pos(s.bildobj, 4, KOPF);
  lv_obj_set_size(s.bildobj, BB, ZH * ZEILEN);
  lv_obj_add_flag(s.bildobj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s.bildobj, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < ZEILEN; i++) {
    lv_obj_t *zl = s.zeile[i] = lv_obj_create(r);
    lv_obj_set_size(zl, 240, ZH);
    lv_obj_set_pos(zl, 0, KOPF + i * ZH);
    lv_obj_set_style_bg_opa(zl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zl, 0, 0);
    lv_obj_set_style_border_color(zl, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_radius(zl, 6, 0);
    lv_obj_set_style_pad_all(zl, 0, 0);
    lv_obj_remove_flag(zl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(zl, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(zl, [](lv_event_t *e) { tippen((int) (intptr_t) lv_event_get_user_data(e)); }, LV_EVENT_CLICKED,
                        (void *) (intptr_t) i);
    s.titel[i] = label(zl, a11->get_lv_font(), 0xFFFFFF);
    lv_obj_set_width(s.titel[i], 116);
    lv_label_set_long_mode(s.titel[i], LV_LABEL_LONG_DOT);
    lv_obj_set_height(s.titel[i], 30);
    lv_obj_set_pos(s.titel[i], BB + 10, 5);
    s.unter[i] = label(zl, a10->get_lv_font(), 0xA0A0A0);
    lv_obj_set_width(s.unter[i], 116);
    lv_label_set_long_mode(s.unter[i], LV_LABEL_LONG_DOT);
    lv_obj_set_height(s.unter[i], 28);
    lv_obj_set_pos(s.unter[i], BB + 10, 38);
    // Dauer unten rechts im Vorschaubild
    s.dauer[i] = label(zl, a10->get_lv_font(), 0xFFFFFF);
    lv_obj_set_style_bg_color(s.dauer[i], lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s.dauer[i], 200, 0);
    lv_obj_set_style_pad_hor(s.dauer[i], 3, 0);
    lv_obj_set_style_radius(s.dauer[i], 3, 0);
    lv_obj_align(s.dauer[i], LV_ALIGN_TOP_LEFT, 4 + BB - 40, (ZH - BH) / 2 + BH - 16);
  }
  s.meldung = label(r, a12->get_lv_font(), 0xFFFFFF);
  lv_obj_set_width(s.meldung, 220);
  lv_label_set_long_mode(s.meldung, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s.meldung, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(s.meldung, lv_color_hex(0x202020), 0);
  lv_obj_set_style_bg_opa(s.meldung, 235, 0);
  lv_obj_set_style_pad_all(s.meldung, 8, 0);
  lv_obj_set_style_radius(s.meldung, 8, 0);
  lv_obj_align(s.meldung, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_add_flag(s.meldung, LV_OBJ_FLAG_HIDDEN);
}

inline void markieren() {
  auto &s = z();
  for (int i = 0; i < ZEILEN; i++)
    lv_obj_set_style_border_width(s.zeile[i], i == s.sel && i < (int) s.v.size() ? 2 : 0, 0);
}

inline void kopf_zeigen() {
  auto &s = z();
  const bool liste = s.kat.rfind("pl:", 0) == 0;
  std::string k = (liste ? "‹ " : "‹ ") + (s.kat_name.empty() ? std::string("Showtime") : s.kat_name) + " ›";
  (lv_label_set_text)(s.kopf, k.c_str());
  char b[24];
  snprintf(b, sizeof(b), "%d/%d%s", s.s + 1, s.n, s.mehr ? "+" : "");
  lv_label_set_text(s.seite, s.v.empty() ? "" : b);
}

inline void zeigen_liste() {
  auto &s = z();
  for (int i = 0; i < ZEILEN; i++) {
    const bool da = i < (int) s.v.size();
    if (da) {
      (lv_label_set_text)(s.titel[i], s.v[i].t.c_str());
      std::string u = s.v[i].liste ? std::string(sprache::englisch() ? "Playlist · " : "Playlist · ") + s.v[i].z : s.v[i].z;
      (lv_label_set_text)(s.unter[i], u.c_str());
      (lv_label_set_text)(s.dauer[i], s.v[i].d.c_str());
      if (s.v[i].d.empty()) lv_obj_add_flag(s.dauer[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_remove_flag(s.dauer[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s.zeile[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s.zeile[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (s.sel >= (int) s.v.size()) s.sel = s.v.empty() ? 0 : (int) s.v.size() - 1;
  markieren();
  kopf_zeigen();
}

// Antwort von HA uebernehmen (Hauptschleife)
inline void anwenden() {
  auto &s = z();
  s.fertig = false;
  if (s.antwort == nullptr) { meldung(sprache::englisch() ? "Home Assistant not reachable" : "Home Assistant nicht erreichbar"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, s.antwort, s.antwort_len)) { meldung("Showtime: ?"); return; }
  s.angemeldet = (doc["a"] | 0) == 1;
  JsonArrayConst k = doc["k"];
  if (!k.isNull()) {
    s.kats.clear();
    for (JsonArrayConst e : k) s.kats.emplace_back(e[0] | "", e[1] | "");
  }
  const char *m = doc["m"] | "";
  if (!s.angemeldet) {
    s.v.clear();
    s.kat_name = "Showtime";
    zeigen_liste();
    lv_obj_add_flag(s.bildobj, LV_OBJ_FLAG_HIDDEN);
    meldung(m, 0);
    return;
  }
  if (!doc["kat"].isNull()) {
    s.kat = doc["kat"] | "empfohlen";
    s.kat_name = doc["kn"] | "";
    s.s = doc["s"] | 0;
    s.n = doc["n"] | 1;
    s.mehr = (doc["mehr"] | 0) == 1;
    s.v.clear();
    for (JsonObjectConst e : doc["v"].as<JsonArrayConst>())
      s.v.push_back({e["t"] | "", e["z"] | "", e["d"] | "", (e["p"] | 0) == 1});
    zeigen_liste();
    s.geladen_ms = millis();
    const std::string b = doc["b"] | "";
    if (!b.empty() && b != s.bild) {
      s.bild = b;
      showtime_bild->set_url("http://" + tk_cfg->host() + b + "&k=" + tk_cfg->schluessel());
      showtime_bild->update();
    }
  }
  if (*m) meldung(m);
  else if (s.meldung_bis == 0 && s.meldung) lv_obj_add_flag(s.meldung, LV_OBJ_FLAG_HIDDEN);
}

// online_image fertig (on_download_finished)
inline void bild_da() {
  auto &s = z();
  if (!s.gebaut) return;
  lv_image_set_src(s.bildobj, showtime_bild->get_lv_image_dsc());
  lv_obj_remove_flag(s.bildobj, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------- Bedienung
inline void laden(const std::string &kat, int seite, int sel = 0) {
  auto &s = z();
  if (s.laeuft || s.fertig) return;
  if (kat.rfind("pl:", 0) != 0 && kat != s.kat) s.vor_liste = kat;
  s.kat = kat;
  s.s = seite < 0 ? 0 : seite;
  s.sel = sel;
  abrufen();
}

inline void kategorie(int schritt) {
  auto &s = z();
  if (s.kats.empty()) { abrufen(); return; }
  if (s.kat.rfind("pl:", 0) == 0) { laden("playlists", 0); return; }   // aus der Playlist: zurueck zur Liste
  int i = 0;
  for (int j = 0; j < (int) s.kats.size(); j++) if (s.kats[j].first == s.kat) i = j;
  i = (i + schritt + (int) s.kats.size()) % (int) s.kats.size();
  s.kat_name = s.kats[i].second;
  laden(s.kats[i].first, 0);
  kopf_zeigen();
}

inline void blaettern(int schritt) {
  auto &s = z();
  const int neu = s.s + schritt;
  if (neu < 0) return;
  if (neu >= s.n && !s.mehr) return;
  laden(s.kat, neu, schritt > 0 ? 0 : ZEILEN - 1);
}

inline void tippen(int i) {
  auto &s = z();
  if (i < 0 || i >= (int) s.v.size() || s.laeuft || s.fertig) return;
  s.sel = i;
  markieren();
  if (!s.v[i].liste) meldung((sprache::englisch() ? "» Playing: " : "» ") + s.v[i].t, 4000);
  abrufen(i);
}

inline void geste(lv_event_t *) {
  lv_indev_t *in = lv_indev_active();
  if (in == nullptr) return;
  switch (lv_indev_get_gesture_dir(in)) {
    case LV_DIR_LEFT: kategorie(1); break;
    case LV_DIR_RIGHT: kategorie(-1); break;
    case LV_DIR_TOP: blaettern(1); break;
    case LV_DIR_BOTTOM: blaettern(-1); break;
    default: break;
  }
}

// Tasten auf der Seite (14 hoch, 35 runter, 15 links, 32 rechts, 34 OK, 13 Zurueck); true = verbraucht
inline bool taste(int k, bool gedrueckt) {
  auto &s = z();
  if (k != 14 && k != 35 && k != 15 && k != 32 && k != 34 && k != 13) return false;
  if (!gedrueckt) return true;
  switch (k) {
    case 14:
      if (s.sel > 0) { s.sel--; markieren(); } else blaettern(-1);
      break;
    case 35:
      if (s.sel < (int) s.v.size() - 1) { s.sel++; markieren(); } else blaettern(1);
      break;
    case 15: kategorie(-1); break;
    case 32: kategorie(1); break;
    case 34: tippen(s.sel); break;
    case 13:
      if (s.kat.rfind("pl:", 0) == 0) laden(s.vor_liste.empty() ? "playlists" : s.vor_liste, 0);
      else go_home->execute();
      break;
  }
  return true;
}

// Seite geladen (on_load)
inline void geladen() {
  auto &s = z();
  aufbauen();
  zeigen_liste();
  // Liste aelter als 5 min (oder noch keine): neu holen - HA haelt sie 10 min vor
  if (s.v.empty() || millis() - s.geladen_ms > 300000) abrufen();
}

inline void oeffnen() {
  popup_seite->value() = -1;
  tasten::seite_zeigen(page_showtime, LV_SCR_LOAD_ANIM_FADE_IN);
}

// alle 250 ms (Intervall der Dock-Anzeige)
inline void takt() {
  auto &s = z();
  if (s.fertig) anwenden();
  if (s.meldung_bis && (int32_t) (millis() - s.meldung_bis) > 0) {
    s.meldung_bis = 0;
    if (s.meldung) lv_obj_add_flag(s.meldung, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace showtime
