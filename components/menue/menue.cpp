#include "menue.h"
#include "esphome/components/tasten_konfig/tasten_konfig.h"
#include "esphome/components/menu_ui/mdi_icons.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <set>
#include <strings.h>

namespace esphome {
namespace menue {

static const char *const TAG = "menue";

// Raster und Farben (wie die eingebauten Menueseiten)
static const int SPALTEN = 6, ZEILE = 40, RAND = 6, LUECKE = 4, MAX_ZEILEN = 128;
static const uint32_t C_KARTE = 0x1A1F27, C_FOKUS = 0x2A3340, C_RAHMEN_F = 0x3B82F6;
static const uint32_t C_TEXT = 0xF1F3F5, C_GRAU = 0x8B94A3, C_AN = 0x22C55E;

static Typ typ_von(const char *t) {
  if (!strcmp(t, "page")) return T_PAGE;
  if (!strcmp(t, "special")) return T_SPECIAL;
  if (!strcmp(t, "action")) return T_ACTION;
  if (!strcmp(t, "toggle")) return T_TOGGLE;
  if (!strcmp(t, "light")) return T_LIGHT;
  if (!strcmp(t, "cover")) return T_COVER;
  if (!strcmp(t, "sensor")) return T_SENSOR;
  if (!strcmp(t, "camera")) return T_KAMERA;
  if (!strcmp(t, "setting")) return T_EINST;
  return T_TEXT;
}

const char *Menue::icon_(const char *name) {
  static char buf[5];
  if (name == nullptr || *name == 0) return nullptr;
  for (const auto &e : menu_ui::MDI_ICONS) {
    if (strcmp(e.name, name) != 0) continue;
    const uint32_t cp = e.cp;  // 4-Byte-UTF-8 (alle MDI-Glyphen liegen > 0xFFFF)
    buf[0] = (char) (0xF0 | (cp >> 18));
    buf[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char) (0x80 | (cp & 0x3F));
    buf[4] = 0;
    return buf;
  }
  return nullptr;
}

// ------------------------------------------------------------------ Daten

void Menue::setup() {
  tk_->bei_neuer_konfig([this]() {
    const bool sichtbar = root_ != nullptr && lv_screen_active() == lv_obj_get_screen(root_) && !items_.empty();
    laden_();
    if (sichtbar) {
      // Stapel auf vorhandene Seiten kuerzen, dann neu zeichnen
      while (!stapel_.empty() && seite_index_(stapel_.back()) < 0) stapel_.pop_back();
      if (stapel_.empty() && aktiv()) stapel_.push_back(start_);
      if (aktiv()) bauen_();
    }
  });
  laden_();
}

int Menue::seite_index_(const std::string &id) const {
  for (size_t i = 0; i < seiten_.size(); i++)
    if (seiten_[i].id.compare(id.c_str()) == 0) return (int) i;
  return -1;
}

void Menue::laden_() {
  seiten_.clear();
  gen_ = tk_->generation();
  JsonObjectConst m = tk_->wurzel()["menu"];
  // Seit 2026-10-04 ersetzt das Konfigurator-Menue das eingebaute immer (kein Umschalter mehr).
  // Ohne geladene Konfiguration bleibt das eingebaute Menue als Rueckfall.
  an_ = !m.isNull() && m["pages"].size() > 0;
  if (m.isNull()) return;
  for (JsonObjectConst p : m["pages"].as<JsonArrayConst>()) {
    const char *id = p["id"] | "";
    if (*id) seiten_.push_back(Seite{PStr(id), p});
  }
  start_ = m["start"] | (seiten_.empty() ? "" : seiten_[0].id.c_str());
  if (seite_index_(start_) < 0 && !seiten_.empty()) start_ = seiten_[0].id.c_str();
  // Zustaende aller Entitaeten abonnieren (einmal je Entitaet/Attribut)
  for (auto &s : seiten_) {
    for (JsonObjectConst it : s.j["items"].as<JsonArrayConst>()) {
      const char *e = it["entity"] | "";
      if (!*e) continue;
      const Typ t = typ_von(it["t"] | "");
      (void) t;  // Zustaende kommen per HTTP-Abfrage, solange die Seite offen ist (abfrage_*)
    }
    // Popup-Ausloeser dieser Seite
    for (const char *art : {"open", "close"})
      for (JsonObjectConst a : s.j["popup"][art].as<JsonArrayConst>()) {
        const char *e = a["entity"] | "";
        const char *at = a["attr"] | "";
        if (*e) abonnieren_(e, *at ? at : nullptr);
      }
  }
  // Neue Entitaeten bekommen ihren Zustand erst beim naechsten Verbinden mit HA. Die Abo-Liste
  // zur Laufzeit neu zu senden (on_subscribe_home_assistant_states_request) traf auch Verbindungen
  // mitten im Aufbau und brachte HAs Einrichtung zum Scheitern (Timeout ListEntities, 2026-10-03).
  if (abo_neu_) ESP_LOGI(TAG, "Neue Entitaeten im Menue - ihr Zustand kommt mit der naechsten HA-Verbindung");
  abo_neu_ = false;
  ESP_LOGI(TAG, "Menue: %u Seiten, Start '%s', %s", (unsigned) seiten_.size(), start_.c_str(), an_ ? "AN" : "aus");
}

void Menue::abonnieren_(const std::string &entity, const char *attr) {
#ifdef USE_API
  if (api::global_api_server == nullptr) return;
  const std::string key = entity + "|" + (attr ? attr : "");
  for (const auto &a : abos_) if (a.compare(key.c_str()) == 0) return;
  abos_.push_back(PStr(key.c_str()));
  const std::string a = attr ? attr : "";
  api::global_api_server->subscribe_home_assistant_state(
      entity, attr ? optional<std::string>(a) : optional<std::string>(), [this, entity, a](StringRef s) {
        EntZustand &z = zustand_cache_[PStr(entity.c_str())];
        std::string alt;
        if (a.empty()) { alt = z.state.c_str(); z.state.assign(s.c_str(), s.size()); }
        else if (a == "brightness") z.bri = s.size() ? atoi(s.c_str()) : -1;
        else if (a == "current_position") z.pos = s.size() ? atoi(s.c_str()) : -1;
        else if (a == "unit_of_measurement") z.unit.assign(s.c_str(), s.size());
        if (!a.empty()) { PStr ak(a.c_str()); auto f = z.attr.find(ak); if (f != z.attr.end()) alt = f->second.c_str(); z.attr[ak].assign(s.c_str(), s.size()); }
        aktualisieren_(entity);
        // Der erste Wert nach dem Start ist kein Wechsel (sonst ginge z. B. der Sleeptimer bei jedem
        // Aufwachen neu auf); spaetere Wiederholungen beim Neuverbinden aendern nichts (alt == neu).
        static std::set<std::string> bekannt;
        if (bekannt.insert(entity + "|" + a).second) return;
        ausloeser_(entity, a, alt, s.str());
      });
  abo_neu_ = true;
#endif
}

// Zustandswechsel -> Popup oeffnen/schliessen.
void Menue::ausloeser_(const std::string &entity, const std::string &attr, const std::string &alt, const std::string &neu) {
  if (alt == neu) return;
  for (auto &s : seiten_) {
    JsonObjectConst pp = s.j["popup"];
    if (pp.isNull()) continue;
    auto passt = [&](JsonObjectConst a) {
      return entity == (a["entity"] | "") && attr == (a["attr"] | "") && strcasecmp(neu.c_str(), a["to"] | "") == 0;
    };
    for (JsonObjectConst a : pp["close"].as<JsonArrayConst>())
      if (passt(a)) {
        ESP_LOGI(TAG, "Popup '%s' schliessen (%s -> %s)", s.id.c_str(), entity.c_str(), neu.c_str());
        if (popup_zu) popup_zu(s.id.c_str());
      }
    for (JsonObjectConst a : pp["open"].as<JsonArrayConst>())
      if (passt(a)) {
        ESP_LOGI(TAG, "Popup '%s' oeffnen (%s -> %s)", s.id.c_str(), entity.c_str(), neu.c_str());
        if (popup_auf) popup_auf(s.id.c_str(), pp["wake"] | true);
        const uint32_t sek = pp["timeout"] | 0;
        if (sek > 0) {
          const std::string id = s.id.c_str();
          set_timeout("popup_zeit", sek * 1000, [this, id]() { if (popup_ && popup_id_ == id && popup_zu) popup_zu(id); });
        } else {
          cancel_timeout("popup_zeit");
        }
        break;
      }
  }
}

// ------------------------------------------------------------------ Zustaende per HTTP

void Menue::loop() {
  if (abf_fertig_) { abf_fertig_ = false; abf_laeuft_ = false; abfrage_auswerten_(); }
  const bool sichtbar = root_ != nullptr && !items_.empty() && lv_screen_active() == lv_obj_get_screen(root_);
  if (hoehen_neu_) hoehen_jetzt_();
  if (sichtbar && (int32_t) (millis() - einst_ab_) >= 0) {
    einst_ab_ = millis() + 700;
    for (auto &it : items_) if (it.typ == T_EINST) einst_zeigen_(it);
  }
  if (!sichtbar || abf_laeuft_ || (int32_t) (millis() - abf_ab_) < 0) return;
  abfrage_starten_();
}

void Menue::abfrage_starten_() {
  std::string liste;
  for (auto &it : items_) {
    const char *e = it.j["entity"] | "";
    if (!*e || it.wert == nullptr) continue;
    if (liste.find(std::string(e) + ",") != std::string::npos) continue;
    liste += e;
    liste += ",";
  }
  abf_ab_ = millis() + 5000;   // alle 5 s, solange die Seite offen ist
  if (liste.empty()) return;
  abf_url_ = "http://" + tk_->host() + "/api/esphomeremote_konfig/states?k=" + tk_->schluessel() + "&e=" + liste;
  abf_laeuft_ = true;
  abf_fertig_ = false;
  // einmal angelegter Arbeits-Task mit PSRAM-Stapel (sonst alle 5 s 4 KB internes RAM an und ab)
  if (worker_ == nullptr) {
    static StaticTask_t tcb;
    const size_t groesse = 4096;
    auto *stapel = (StackType_t *) heap_caps_malloc(groesse, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stapel != nullptr)
      worker_ = xTaskCreateStaticPinnedToCore(abfrage_task_, "mn_abf", groesse, this, 1, stapel, &tcb, tskNO_AFFINITY);
  }
  if (worker_ != nullptr) xTaskNotifyGive((TaskHandle_t) worker_);
  else abf_laeuft_ = false;
}

void Menue::abfrage_task_(void *arg) {
  auto *self = (Menue *) arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    abfrage_einmal_(self);
  }
}

void Menue::abfrage_einmal_(Menue *self) {
  esp_http_client_config_t cfg = {};
  cfg.url = self->abf_url_.c_str();
  cfg.timeout_ms = 3000;
  cfg.buffer_size = 512;
  cfg.keep_alive_enable = false;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  int status = -1;
  size_t len = 0;
  char *buf = nullptr;
  if (c != nullptr && esp_http_client_open(c, 0) == ESP_OK) {
    esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    if (status == 200) {
      const size_t MAX = 8192;
      buf = (char *) heap_caps_malloc(MAX, MALLOC_CAP_SPIRAM);
      while (buf != nullptr && len < MAX) {
        int r = esp_http_client_read(c, buf + len, MAX - len);
        if (r <= 0) break;
        len += r;
      }
    }
  }
  if (c != nullptr) { esp_http_client_close(c); esp_http_client_cleanup(c); }
  self->abf_buf_ = buf;
  self->abf_len_ = len;
  self->abf_status_ = status;
  self->abf_fertig_ = true;
}

// Antwort: {"light.x": ["on", 128, -1, ""], ...}  (Zustand, Helligkeit 0-255, Position, Einheit)
void Menue::abfrage_auswerten_() {
  if (abf_status_ == 200 && abf_buf_ != nullptr && abf_len_ > 0) {
    json::SpiRamAllocator alloc;
    JsonDocument doc(&alloc);
    if (!deserializeJson(doc, abf_buf_, abf_len_)) {
      for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
        EntZustand &z = zustand_cache_[PStr(kv.key().c_str())];
        JsonArrayConst a = kv.value();
        z.state = a[0] | "";
        z.bri = a[1] | -1;
        z.pos = a[2] | -1;
        z.unit = a[3] | "";
      }
      for (auto &it : items_) zustand_(it);
    }
  } else if (abf_status_ != 200) {
    ESP_LOGD(TAG, "Zustandsabfrage fehlgeschlagen (HTTP %d)", abf_status_);
  }
  free(abf_buf_);
  abf_buf_ = nullptr;
}

// ------------------------------------------------------------------ Navigation

void Menue::popup(const std::string &id) {
  if (seite_index_(id) < 0) { ESP_LOGW(TAG, "Popup-Seite '%s' fehlt", id.c_str()); return; }
  popup_ = true;
  popup_id_ = id;
  einstieg_ = -1;
  stapel_.clear();
  stapel_.push_back(id);
  fokus_mem_.erase(PStr(id.c_str()));
  bauen_();
}

void Menue::oeffnen(bool merken) {
  if (!aktiv()) return;
  popup_ = false;
  einstieg_ = -1;
  if (merken && !gemerkt_.empty()) {
    stapel_ = gemerkt_;
    while (!stapel_.empty() && seite_index_(stapel_.back()) < 0) stapel_.pop_back();
  } else {
    stapel_.clear();
    fokus_mem_.clear();
  }
  if (stapel_.empty()) stapel_.push_back(start_);
  bauen_();
}

void Menue::anzeigen() {
  if (!aktiv()) return;
  popup_ = false;
  einstieg_ = -1;
  if (stapel_.empty()) stapel_.push_back(start_);
  bauen_();
}

bool Menue::zurueck() {
  if (stapel_.size() <= 1) return false;
  fokus_mem_.erase(PStr(stapel_.back().c_str()));  // verlassene Seite vergessen (wie die eingebauten Seiten)
  stapel_.pop_back();
  bauen_();
  return true;
}

void Menue::verlassen() {
  if (popup_) { leeren_(); return; }  // Popup: Menue-Erinnerung nicht anfassen
  // Merken: bis zur tiefsten Seite mit "merken", sonst nichts
  gemerkt_.clear();
  int bis = -1;
  for (int i = (int) stapel_.size() - 1; i >= 0; i--) {
    const int si = seite_index_(stapel_[i]);
    if (si >= 0 && (seiten_[si].j["remember"] | true)) { bis = i; break; }
  }
  if (bis >= 0) gemerkt_.assign(stapel_.begin(), stapel_.begin() + bis + 1);
  if (!stapel_.empty() && fokus_ >= 0) fokus_mem_[PStr(stapel_.back().c_str())] = fokus_;
  leeren_();
}

// ------------------------------------------------------------------ Aufbau

void Menue::leeren_() {
  if (halt_ >= 0) halten_(halt_, false);   // nie eine Bewegung "haengen" lassen
  tmodus_ = false;
  if (fokus_ == FOKUS_ZURUECK) fokus_setzen_(-1);
  if (root_ != nullptr) lv_obj_clean(root_);
  items_.clear();
  fokus_ = -1;
}

// Rasterzeilen fuer einen Text-Eintrag mit automatischer Hoehe (Hinweis einer Einstellung oder langer Text)
int Menue::auto_zeilen_(const Item &it, float zelle) const {
  const char *txt = (it.typ == T_EINST && it.einst.wert != nullptr) ? lv_label_get_text(it.einst.wert) : (it.j["label"] | "");
  if (txt == nullptr || !*txt || (txt[0] == ' ' && !txt[1])) return 1;
  lv_point_t g;
  lv_text_get_size(&g, txt, f_klein_, 0, 0, (int) (it.w * zelle) - LUECKE - 8, LV_TEXT_FLAG_NONE);
  return std::max(1, std::min(8, (int) ((g.y + 8 + ZEILE - 1) / ZEILE)));
}

void Menue::bauen_() {
  if (root_ == nullptr || stapel_.empty()) return;
  ESP_LOGD("ram", "Seite '%s': intern frei %u, groesster Block %u", stapel_.back().c_str(),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  const int si = seite_index_(stapel_.back());
  if (si < 0) return;
  leeren_();
  const Seite &s = seiten_[si];
  if (titel_ != nullptr) lv_label_set_text(titel_, s.j["title"] | s.id.c_str());
  if (zurueck_lbl_ != nullptr) lv_label_set_text(zurueck_lbl_, stapel_.size() <= 1 ? "X" : "<");

  const bool flow = s.j["flow"] | true;
  int W = lv_obj_get_content_width(root_);
  if (W <= 0) W = 240;
  const float zelle = (float) (W - 2 * RAND + LUECKE) / SPALTEN;

  // Belegung fuer den Fluss: bis MAX_ZEILEN Zeilen x 6 Spalten
  uint8_t belegt[MAX_ZEILEN] = {0};
  auto frei = [&](int x, int y, int w, int h) {
    if (x + w > SPALTEN || y + h > MAX_ZEILEN) return false;
    for (int r = y; r < y + h; r++)
      for (int c = x; c < x + w; c++)
        if (belegt[r] & (1 << c)) return false;
    return true;
  };
  int cur = 0;  // Fluss-Cursor (Zelle, zeilenweise)

  JsonArrayConst arr = s.j["items"];
  items_.reserve(arr.size());
  // Einstellungen: erst die eingebauten Seiten auffrischen (aktuelle Werte und Hinweistexte)
  bool hat_einst = false;
  for (JsonObjectConst j : arr) if (strcmp(j["t"] | "", "setting") == 0) hat_einst = true;
  if (hat_einst && einst_auffrischen) einst_auffrischen();
  for (JsonObjectConst j : arr) {
    Item it;
    it.j = j;
    it.typ = typ_von(j["t"] | "");
    it.w = std::max(1, std::min(6, (int) (j["w"] | 6)));
    if (it.typ == T_EINST && einstellung) it.einst = einstellung(j["src"] | "");
    // h = 0: Hoehe nach dem Text (Hinweise/Erklaerungen), sonst 1..8 Zeilen
    it.h = (int) (j["h"] | 1) <= 0 ? auto_zeilen_(it, zelle) : std::max(1, std::min(8, (int) (j["h"] | 1)));
    if (flow) {
      int pos = cur;
      while (pos < MAX_ZEILEN * SPALTEN && !frei(pos % SPALTEN, pos / SPALTEN, it.w, it.h)) pos++;
      it.x = pos % SPALTEN;
      it.y = pos / SPALTEN;
      cur = pos + it.w;
    } else {
      it.x = std::max(0, std::min(SPALTEN - it.w, (int) (j["x"] | 0)));
      it.y = std::max(0, std::min(MAX_ZEILEN - 1, (int) (j["y"] | 0)));
    }
    for (int r = it.y; r < it.y + it.h && r < MAX_ZEILEN; r++)
      for (int c = it.x; c < it.x + it.w; c++) belegt[r] |= (1 << c);
    items_.push_back(it);
  }
  for (size_t i = 0; i < items_.size(); i++) {
    Item &it = items_[i];
    karte_(it);
    lv_obj_set_pos(it.obj, RAND + (int) (it.x * zelle), RAND + it.y * ZEILE);
    lv_obj_set_size(it.obj, (int) (it.w * zelle) - LUECKE, it.h * ZEILE - LUECKE);
    if (it.fokusierbar()) {
      lv_obj_set_user_data(it.obj, (void *) (intptr_t) i);
      lv_obj_add_flag(it.obj, LV_OBJ_FLAG_CLICKABLE);
      if (it.j["hold"] | false) {
        // Halte-Knopf: Aktion beim Druecken, "release"-Schritte beim Loslassen (z. B. PTZ)
        lv_obj_add_event_cb(it.obj, Menue::halt_cb_, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(it.obj, Menue::halt_cb_, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(it.obj, Menue::halt_cb_, LV_EVENT_PRESS_LOST, this);
        lv_obj_remove_flag(it.obj, LV_OBJ_FLAG_SCROLL_CHAIN);
      } else {
        lv_obj_add_event_cb(it.obj, Menue::klick_cb_, LV_EVENT_CLICKED, this);
      }
    }
    zustand_(it);
  }
  // Fokus: gemerkt oder erstes bedienbares Element
  auto f = fokus_mem_.find(PStr(stapel_.back().c_str()));
  int start = (f != fokus_mem_.end() && f->second < (int) items_.size()) ? f->second : -1;
  if (start < 0)
    for (size_t i = 0; i < items_.size(); i++)
      if (items_[i].fokusierbar()) { start = (int) i; break; }
  lv_obj_scroll_to_y(root_, 0, LV_ANIM_OFF);
  fokus_setzen_(start >= 0 ? start : FOKUS_ZURUECK);
  abf_ab_ = millis();   // Zustaende sofort holen
}

static lv_obj_t *text_(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t farbe) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, t);
  if (f) lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  return l;
}

void Menue::karte_(Item &it) {
  const char *label = it.j["label"] | "";
  if (it.typ == T_TEXT) {
    lv_obj_t *o = lv_obj_create(root_);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_t *l = text_(o, label, f_klein_, C_GRAU);
    lv_obj_set_width(l, LV_PCT(100));
    if (it.h >= 2 || (int) (it.j["h"] | 1) <= 0) {   // mehrzeilig: Fliesstext von oben
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_align(l, LV_ALIGN_TOP_LEFT, 2, 2);
    } else {
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
      lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 2, -2);
    }
    it.obj = o;
    return;
  }
  if (it.typ == T_EINST && it.einst.typ == 'i' && !*label) {
    // Hinweis/Erklaerung einer Einstellung: Fliesstext in Farbe und Wortlaut des Originals
    lv_obj_t *o = lv_obj_create(root_);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    it.wert = text_(o, "", f_klein_, C_GRAU);
    lv_label_set_long_mode(it.wert, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(it.wert, LV_PCT(100));
    lv_obj_align(it.wert, LV_ALIGN_TOP_LEFT, 2, 0);
    it.obj = o;
    return;
  }
  if (it.typ == T_KAMERA) {
    // Kamerabild: Bild mittig, schwarzer Rahmen; Platzhaltertext, solange keins geladen ist
    lv_obj_t *k = lv_obj_create(root_);
    lv_obj_remove_style_all(k);
    lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(k, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(k, 8, 0);
    lv_obj_set_style_clip_corner(k, true, 0);
    lv_obj_clear_flag(k, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_t *ph = text_(k, "Bild wird geladen ...", f_klein_, C_GRAU);
    lv_obj_center(ph);
    lv_obj_t *img = lv_image_create(k);
    lv_obj_center(img);
    it.obj = k;
    it.regler = img;   // Bild-Widget
    it.wert = ph;      // Platzhalter
    const void *src = bild ? bild(it.j["src"] | "") : nullptr;
    if (src) { lv_image_set_src(img, src); lv_obj_add_flag(ph, LV_OBJ_FLAG_HIDDEN); }
    return;
  }
  lv_obj_t *k = lv_obj_create(root_);
  lv_obj_remove_style_all(k);
  lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(k, lv_color_hex(C_KARTE), 0);
  lv_obj_set_style_bg_color(k, lv_color_hex(C_FOKUS), LV_STATE_PRESSED);
  lv_obj_set_style_radius(k, 10, 0);
  lv_obj_set_style_border_width(k, 2, 0);
  lv_obj_set_style_border_color(k, lv_color_hex(C_KARTE), 0);
  lv_obj_set_style_pad_hor(k, it.w <= 2 ? 3 : 10, 0);
  lv_obj_set_style_pad_ver(k, 4, 0);
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(k, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  it.obj = k;

  const bool klein = it.w <= 2;
  const bool hoch = it.h >= 2;
  const char *ic = icon_(it.j["icon"] | "");
  const lv_font_t *f = klein ? f_klein_ : f_text_;
  const bool mit_wert = it.typ == T_TOGGLE || it.typ == T_LIGHT || it.typ == T_COVER || it.typ == T_SENSOR ||
                        (it.typ == T_EINST && it.einst.wert != nullptr);
  if (it.typ == T_EINST && it.einst.typ != 'b' && it.einst.typ != 's') {
    // reine Anzeige: ohne Kartenhintergrund, Name links, Wert rechts
    lv_obj_set_style_bg_opa(k, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(k, 0, 0);
  }
  if (hoch && it.typ == T_EINST && it.einst.typ == 's' && it.einst.obj != nullptr) {
    // Kopfzeile Name / Wert, darunter ein Regler mit dem Bereich des Originals
    lv_obj_t *l = text_(k, label, f, C_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(62));
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 4);
    it.wert = text_(k, "", f_klein_, C_GRAU);
    lv_obj_align(it.wert, LV_ALIGN_TOP_RIGHT, 0, 6);
    lv_obj_t *sl = lv_slider_create(k);
    lv_slider_set_range(sl, lv_slider_get_min_value(it.einst.obj), lv_slider_get_max_value(it.einst.obj));
    lv_obj_set_width(sl, LV_PCT(90));
    lv_obj_set_height(sl, 8);
    lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x4B5563), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_RAHMEN_F), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_TEXT), LV_PART_KNOB);
    lv_obj_set_user_data(sl, (void *) (intptr_t) (&it - items_.data()));
    lv_obj_add_event_cb(sl, [](lv_event_t *e) {
      auto *self = (Menue *) lv_event_get_user_data(e);
      lv_obj_t *s = (lv_obj_t *) lv_event_get_target(e);
      const int i = (int) (intptr_t) lv_obj_get_user_data(s);
      if (i < 0 || i >= (int) self->items_.size()) return;
      Item &it = self->items_[i];
      lv_slider_set_value(it.einst.obj, lv_slider_get_value(s), LV_ANIM_OFF);
      lv_obj_send_event(it.einst.obj, LV_EVENT_VALUE_CHANGED, nullptr);
      self->einst_zeigen_(it);
    }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(sl, [](lv_event_t *e) { ((Menue *) lv_event_get_user_data(e))->hoehen_pruefen_(); }, LV_EVENT_RELEASED, this);
    it.regler = sl;
    return;
  }

  if (hoch && (it.typ == T_LIGHT || it.typ == T_COVER)) {
    // Kopfzeile: Name links, Wert rechts; darunter Regler bzw. Rollo-Knoepfe
    lv_obj_t *l = text_(k, label, f, C_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(65));
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 4);
    it.wert = text_(k, "", f_klein_, C_GRAU);
    lv_obj_align(it.wert, LV_ALIGN_TOP_RIGHT, 0, 6);
    if (it.typ == T_LIGHT) {
      lv_obj_t *sl = lv_slider_create(k);
      lv_slider_set_range(sl, 0, 100);
      lv_obj_set_width(sl, LV_PCT(90));
      lv_obj_set_height(sl, 8);
      lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -8);
      lv_obj_set_style_bg_color(sl, lv_color_hex(0x4B5563), LV_PART_MAIN);
      lv_obj_set_style_bg_color(sl, lv_color_hex(C_RAHMEN_F), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(sl, lv_color_hex(C_TEXT), LV_PART_KNOB);
      lv_obj_set_user_data(sl, (void *) (intptr_t) (&it - items_.data()));
      lv_obj_add_event_cb(sl, Menue::regler_cb_, LV_EVENT_RELEASED, this);
      it.regler = sl;
    } else {
      static const char *const T[3] = {"Zu", "Stop", "Auf"};
      for (int d = 0; d < 3; d++) {
        lv_obj_t *b = lv_obj_create(k);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x232A35), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_RAHMEN_F), LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_size(b, LV_PCT(31), 26);
        lv_obj_align(b, d == 0 ? LV_ALIGN_BOTTOM_LEFT : d == 1 ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_BOTTOM_RIGHT, 0, -2);
        lv_obj_center(text_(b, T[d], f_klein_, C_TEXT));
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(b, (void *) (intptr_t) ((&it - items_.data()) * 4 + d));
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
          auto *self = (Menue *) lv_event_get_user_data(e);
          const int v = (int) (intptr_t) lv_obj_get_user_data((lv_obj_t *) lv_event_get_target(e));
          if (v / 4 < (int) self->items_.size()) self->ausloesen_(self->items_[v / 4], (v % 4) - 1 == 0 ? 2 : (v % 4) - 1);
        }, LV_EVENT_CLICKED, this);
      }
    }
    return;
  }

  if (hoch || klein) {
    // Kachel: Icon oben, Name darunter, Wert ganz unten (zentriert)
    lv_obj_t *ico = nullptr;
    if (ic && f_icon_) { ico = text_(k, ic, f_icon_, C_TEXT); }
    lv_obj_t *l = text_(k, label, f, C_TEXT);
    lv_label_set_long_mode(l, hoch ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (mit_wert) {
      it.wert = text_(k, "", f_klein_, C_GRAU);
      lv_obj_align(it.wert, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    if (ico && hoch) {
      lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 2);
      lv_obj_align(l, LV_ALIGN_CENTER, 0, it.h >= 3 ? 4 : 8);
    } else if (ico && !mit_wert && !label[0]) {
      lv_obj_center(ico);
    } else {
      if (ico) lv_obj_delete(ico);
      lv_obj_align(l, LV_ALIGN_CENTER, 0, (mit_wert && hoch) ? -6 : 0);
    }
    if (mit_wert && !hoch) {  // klein & einzeilig: Zustand nur als Farbe des Namens
      lv_obj_delete(it.wert);
      it.wert = l;
      it.name_wert = true;
    }
    return;
  }

  // Listenzeile: [Icon] Name ............ Wert / >
  int x = 0;
  if (ic && f_icon_) {
    lv_obj_t *ico = text_(k, ic, f_icon_, C_TEXT);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);
    x = 26;
  }
  lv_obj_t *l = text_(k, label, f, C_TEXT);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, LV_PCT(mit_wert ? 60 : 80));
  lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
  if (mit_wert) {
    it.wert = text_(k, "", f_klein_, C_GRAU);
    lv_obj_align(it.wert, LV_ALIGN_RIGHT_MID, 0, 0);
  } else if (it.typ == T_PAGE || it.typ == T_SPECIAL) {
    lv_obj_align(text_(k, ">", f_text_, C_GRAU), LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

void Menue::bild_neu() {
  for (auto &it : items_) {
    if (it.typ != T_KAMERA || it.regler == nullptr) continue;
    const void *src = bild ? bild(it.j["src"] | "") : nullptr;
    if (!src) continue;
    lv_image_set_src(it.regler, src);
    lv_obj_invalidate(it.regler);
    if (it.wert) lv_obj_add_flag(it.wert, LV_OBJ_FLAG_HIDDEN);
  }
}

void Menue::einst_zeigen_(Item &it) {
  if (it.einst.wert != nullptr && it.wert != nullptr) {
    const lv_color_t farbe = lv_obj_get_style_text_color(it.einst.wert, LV_PART_MAIN);
    if (!it.name_wert) {
      const char *neu = lv_label_get_text(it.einst.wert);
      if (strcmp(lv_label_get_text(it.wert), neu) != 0) lv_label_set_text(it.wert, neu);
    }
    lv_obj_set_style_text_color(it.wert, farbe, 0);
  }
  if (it.regler != nullptr && it.einst.obj != nullptr && !lv_obj_has_state(it.regler, LV_STATE_PRESSED))
    lv_slider_set_value(it.regler, lv_slider_get_value(it.einst.obj), LV_ANIM_OFF);
}

// Regler per Taste/Antippen um d Stufen verstellen (Tippen auf einen einzeiligen Regler: weiterzaehlen, am Ende von vorn)
void Menue::einst_schritt_(Item &it, int d) {
  lv_obj_t *o = it.einst.obj;
  if (o == nullptr) return;
  const int lo = lv_slider_get_min_value(o), hi = lv_slider_get_max_value(o);
  int v = lv_slider_get_value(o) + d;
  if (d == 0) v = lv_slider_get_value(o) >= hi ? lo : lv_slider_get_value(o) + 1;
  v = std::max(lo, std::min(hi, v));
  lv_slider_set_value(o, v, LV_ANIM_OFF);
  lv_obj_send_event(o, LV_EVENT_VALUE_CHANGED, nullptr);
  einst_zeigen_(it);
  hoehen_pruefen_();
}

// Texte mit automatischer Hoehe gewachsen/geschrumpft (Hinweis haengt vom Wert ab)? Dann Seite neu legen.
void Menue::hoehen_pruefen_() { hoehen_neu_ = true; }   // erst in loop(): nicht im Ereignis des Widgets loeschen

void Menue::hoehen_jetzt_() {
  hoehen_neu_ = false;
  if (items_.empty() || root_ == nullptr) return;
  int W = lv_obj_get_content_width(root_);
  if (W <= 0) W = 240;
  const float zelle = (float) (W - 2 * RAND + LUECKE) / SPALTEN;
  for (auto &it : items_) {
    if ((int) (it.j["h"] | 1) > 0) continue;
    if (auto_zeilen_(it, zelle) != it.h) {
      const int f = fokus_;
      const int32_t sy = lv_obj_get_scroll_y(root_);
      bauen_();
      if (f >= 0 && f < (int) items_.size()) fokus_setzen_(f);
      lv_obj_scroll_to_y(root_, sy, LV_ANIM_OFF);
      return;
    }
  }
}

void Menue::zustand_(Item &it) {
  if (it.typ == T_EINST) { einst_zeigen_(it); return; }
  if (it.wert == nullptr || it.typ == T_KAMERA) return;
  const char *e = it.j["entity"] | "";
  auto f = zustand_cache_.find(PStr(e));
  if (!*e || f == zustand_cache_.end() || f->second.state.empty()) {
    if (!it.name_wert) lv_label_set_text(it.wert, "–");
    return;
  }
  const EntZustand &z = f->second;
  const bool an = z.state == "on" || z.state == "open" || z.state == "playing";
  char b[40];
  switch (it.typ) {
    case T_LIGHT:
      if (z.state == "on" && z.bri >= 0) snprintf(b, sizeof(b), "an · %d %%", (z.bri * 100 + 127) / 255);
      else snprintf(b, sizeof(b), "%s", z.state == "on" ? "an" : z.state == "off" ? "aus" : z.state.c_str());
      if (it.regler) lv_slider_set_value(it.regler, z.state == "on" ? (z.bri >= 0 ? (z.bri * 100 + 127) / 255 : 100) : 0, LV_ANIM_OFF);
      break;
    case T_COVER:
      if (z.pos >= 0) snprintf(b, sizeof(b), "%d %%", z.pos);
      else snprintf(b, sizeof(b), "%s", z.state == "open" ? "offen" : z.state == "closed" ? "zu" : z.state.c_str());
      break;
    case T_SENSOR:
      snprintf(b, sizeof(b), "%s%s%s", z.state.c_str(), z.unit.empty() ? "" : " ", z.unit.c_str());
      break;
    default:
      snprintf(b, sizeof(b), "%s", z.state == "on" ? "an" : z.state == "off" ? "aus" : z.state.c_str());
  }
  if (it.name_wert) {
    lv_obj_set_style_text_color(it.wert, lv_color_hex(an ? C_AN : C_TEXT), 0);
    return;
  }
  lv_label_set_text(it.wert, b);
  lv_obj_set_style_text_color(it.wert, lv_color_hex(an && it.typ != T_SENSOR ? C_AN : C_GRAU), 0);
}

void Menue::aktualisieren_(const std::string &entity) {
  for (auto &it : items_) {
    const char *e = it.j["entity"] | "";
    if (entity == e) zustand_(it);
  }
}

// ------------------------------------------------------------------ Bedienung

void Menue::klick_cb_(lv_event_t *e) {
  auto *self = (Menue *) lv_event_get_user_data(e);
  const int i = (int) (intptr_t) lv_obj_get_user_data((lv_obj_t *) lv_event_get_current_target(e));
  if (i < 0 || i >= (int) self->items_.size()) return;
  self->fokus_setzen_(i);
  self->ausloesen_(self->items_[i]);
}

void Menue::halt_cb_(lv_event_t *e) {
  auto *self = (Menue *) lv_event_get_user_data(e);
  const int i = (int) (intptr_t) lv_obj_get_user_data((lv_obj_t *) lv_event_get_current_target(e));
  if (i < 0 || i >= (int) self->items_.size()) return;
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) { self->fokus_setzen_(i); self->halten_(i, true); }
  else self->halten_(i, false);
}

void Menue::halten_(int idx, bool an) {
  if (idx < 0 || idx >= (int) items_.size()) { halt_ = -1; return; }
  Item &it = items_[idx];
  if (an) {
    if (halt_ >= 0 && halt_ != idx) halten_(halt_, false);
    if (halt_ == idx) return;   // Tasten-Wiederholung: laeuft schon
    halt_ = idx;
    JsonArrayConst s = it.j["steps"];
    if (!s.isNull()) tk_->schritte(s);
  } else {
    if (halt_ != idx) return;
    halt_ = -1;
    JsonArrayConst s = it.j["release"];
    if (!s.isNull()) tk_->schritte(s);
  }
}

void Menue::modus_anzeigen_() {
  // Knopf(e) mit der Funktion "keymode" blau umranden, solange der Tastenmodus an ist
  for (auto &it : items_) {
    bool km = false;
    for (JsonObjectConst s : it.j["steps"].as<JsonArrayConst>())
      if (strcmp(s["fn"] | "", "keymode") == 0) km = true;
    if (!km || it.obj == nullptr) continue;
    lv_obj_set_style_outline_width(it.obj, tmodus_ ? 3 : 0, 0);
    lv_obj_set_style_outline_color(it.obj, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_outline_pad(it.obj, -3, 0);
  }
}

void Menue::tastenmodus(int an) {
  const bool neu = an < 0 ? !tmodus_ : an > 0;
  if (!neu && halt_ >= 0) halten_(halt_, false);
  tmodus_ = neu;
  ESP_LOGI(TAG, "Tastenmodus %s", tmodus_ ? "an" : "aus");
  modus_anzeigen_();
}

bool Menue::taste(int k, bool gedrueckt) {
  if (items_.empty()) return false;
  if (tmodus_) {
    if (k == 13) {   // Zurueck: nur den Tastenmodus verlassen, Seite bleibt offen
      if (gedrueckt) tastenmodus(0);
      return true;
    }
    for (size_t i = 0; i < items_.size(); i++) {
      if ((int) (items_[i].j["key"] | 0) != k) continue;
      if (items_[i].j["hold"] | false) halten_((int) i, gedrueckt);
      else if (gedrueckt) ausloesen_(items_[i]);
      return true;
    }
    // Tasten ohne Zuordnung: D-Pad schlucken (keine Fokus-Spruenge), Rest normal
    return k == 14 || k == 35 || k == 15 || k == 32 || k == 34;
  }
  // OK auf einem Halte-Knopf: halten wie mit dem Finger
  if (k == 34 && ((fokus_ >= 0 && fokus_ < (int) items_.size() && (items_[fokus_].j["hold"] | false)) || halt_ >= 0)) {
    if (gedrueckt) halten_(fokus_, true); else if (halt_ >= 0) halten_(halt_, false);
    return true;
  }
  return false;
}

void Menue::regler_cb_(lv_event_t *e) {
  auto *self = (Menue *) lv_event_get_user_data(e);
  lv_obj_t *sl = (lv_obj_t *) lv_event_get_target(e);
  const int i = (int) (intptr_t) lv_obj_get_user_data(sl);
  if (i < 0 || i >= (int) self->items_.size()) return;
  const char *ent = self->items_[i].j["entity"] | "";
  const int v = lv_slider_get_value(sl);
  if (v <= 0) self->tk_->ha_senden("light.turn_off", {{"entity_id", ent}});
  else self->tk_->ha_senden("light.turn_on", {{"entity_id", ent}, {"brightness_pct", std::to_string(v)}});
}

// richtung: 0 = Tippen/OK, -1/+1 = Links/Rechts (Licht dimmen, Rollo zu/auf), 2 = Rollo Stop
void Menue::ausloesen_(Item &it, int richtung) {
  abf_ab_ = millis() + 900;   // kurz danach neuen Zustand holen
  const std::string e = it.j["entity"] | "";
  JsonArrayConst steps = it.j["steps"];
  const bool eigene = !steps.isNull() && steps.size() > 0;
  auto ha = [&](const char *svc) { if (!e.empty()) tk_->ha_senden(svc, {{"entity_id", e}}); };
  switch (it.typ) {
    case T_PAGE: {
      const char *z = it.j["target"] | "";
      if (seite_index_(z) < 0) { ESP_LOGW(TAG, "Seite '%s' fehlt", z); return; }
      if (fokus_ >= 0) fokus_mem_[PStr(stapel_.back().c_str())] = fokus_;
      stapel_.push_back(z);
      bauen_();
      return;
    }
    case T_SPECIAL: {
      if (fokus_ >= 0) fokus_mem_[PStr(stapel_.back().c_str())] = fokus_;
      einstieg_merken_ = it.j["remember"] | false;
      const int n = spezial ? spezial(it.j["page"] | "") : -1;
      einstieg_ = n;
      return;
    }
    case T_LIGHT:
      if (richtung == -1 || richtung == 1) {
        auto f = zustand_cache_.find(PStr(e.c_str()));
        int b = (f != zustand_cache_.end() && f->second.state == "on" && f->second.bri >= 0) ? (f->second.bri * 100 + 127) / 255 : 0;
        b = std::max(0, std::min(100, b + richtung * 10));
        if (b == 0) ha("light.turn_off");
        else tk_->ha_senden("light.turn_on", {{"entity_id", e}, {"brightness_pct", std::to_string(b)}});
        return;
      }
      if (eigene) tk_->schritte(steps); else ha("light.toggle");
      return;
    case T_COVER:
      if (richtung == -1) { ha("cover.close_cover"); return; }
      if (richtung == 1) { ha("cover.open_cover"); return; }
      if (richtung == 2) { ha("cover.stop_cover"); return; }
      if (eigene) tk_->schritte(steps); else ha("cover.toggle");
      return;
    case T_TOGGLE:
      if (eigene) tk_->schritte(steps); else ha("homeassistant.toggle");
      return;
    case T_ACTION:
    case T_SENSOR:
      if (eigene) tk_->schritte(steps);
      return;
    case T_EINST:
      if (it.einst.obj == nullptr) return;
      if (it.einst.typ == 's') { einst_schritt_(it, richtung == 2 ? 0 : richtung); return; }
      if (it.einst.typ == 'b') {
        lv_obj_send_event(it.einst.obj, LV_EVENT_CLICKED, nullptr);
        for (auto &x : items_) if (x.typ == T_EINST) einst_zeigen_(x);   // z. B. Auswahlgruppen
        hoehen_pruefen_();
      }
      return;
    default:
      return;
  }
}

void Menue::fokus_setzen_(int idx) {
  // alten Fokus zuruecksetzen
  if (fokus_ == FOKUS_ZURUECK && zurueck_btn_ != nullptr) {
    lv_obj_set_style_bg_color(zurueck_btn_, lv_color_hex(C_KARTE), 0);
    lv_obj_set_style_border_color(zurueck_btn_, lv_color_hex(C_KARTE), 0);
    if (zurueck_lbl_) lv_obj_set_style_text_color(zurueck_lbl_, lv_color_hex(C_GRAU), 0);
  } else if (fokus_ >= 0 && fokus_ < (int) items_.size()) {
    lv_obj_t *o = items_[fokus_].obj;
    lv_obj_set_style_bg_color(o, lv_color_hex(C_KARTE), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_KARTE), 0);
  }
  fokus_ = idx;
  if (idx == FOKUS_ZURUECK && zurueck_btn_ != nullptr) {
    // wie die eingebauten Seiten: Knopf hell hinterlegt, Zeichen weiss
    lv_obj_set_style_bg_color(zurueck_btn_, lv_color_hex(C_FOKUS), 0);
    lv_obj_set_style_border_color(zurueck_btn_, lv_color_hex(C_RAHMEN_F), 0);
    if (zurueck_lbl_) lv_obj_set_style_text_color(zurueck_lbl_, lv_color_hex(C_TEXT), 0);
    lv_obj_scroll_to_y(root_, 0, LV_ANIM_ON);
    return;
  }
  if (idx < 0 || idx >= (int) items_.size()) return;
  lv_obj_t *o = items_[idx].obj;
  lv_obj_set_style_bg_color(o, lv_color_hex(C_FOKUS), 0);
  lv_obj_set_style_border_color(o, lv_color_hex(C_RAHMEN_F), 0);
  lv_obj_scroll_to_view(o, LV_ANIM_ON);
}

void Menue::fokus(int dx, int dy) {
  int erstes = -1, letztes = -1;
  for (size_t i = 0; i < items_.size(); i++)
    if (items_[i].fokusierbar()) { if (erstes < 0) erstes = (int) i; letztes = (int) i; }
  // Reihenfolge wie auf den eingebauten Seiten: Eintraege ..., dann X/Zurueck, dann wieder oben.
  if (fokus_ == FOKUS_ZURUECK || fokus_ < 0) {
    if (erstes < 0) { fokus_setzen_(FOKUS_ZURUECK); return; }
    fokus_setzen_((dy < 0 || dx < 0) ? letztes : erstes);
    return;
  }
  Item &a = items_[fokus_];
  // Links/Rechts auf breitem Licht/Rollo: Wert aendern statt Fokus bewegen
  if (dx != 0 && dy == 0 && a.w >= 4 && (a.typ == T_LIGHT || a.typ == T_COVER)) { ausloesen_(a, dx); return; }
  if (dx != 0 && dy == 0 && a.typ == T_EINST && a.einst.typ == 's') { ausloesen_(a, dx); return; }
  const float ax = a.x + a.w / 2.0f, ay = a.y + a.h / 2.0f;
  int best = -1, weit = -1;
  float best_d = 1e9f, weit_d = -1;
  for (size_t i = 0; i < items_.size(); i++) {
    if ((int) i == fokus_ || !items_[i].fokusierbar()) continue;
    const Item &b = items_[i];
    const float bx = b.x + b.w / 2.0f, by = b.y + b.h / 2.0f;
    float haupt, neben;
    if (dy != 0) {
      const bool richtig = dy > 0 ? b.y >= a.y + a.h : b.y + b.h <= a.y;
      if (!richtig) continue;
      haupt = std::abs(by - ay);
      neben = std::abs(bx - ax);
    } else {
      const bool gleiche_zeile = b.y < a.y + a.h && b.y + b.h > a.y;
      if (!gleiche_zeile) continue;
      // fuers Umlaufen: das am weitesten entfernte Element auf der Gegenseite merken
      const float gegen = dx > 0 ? ax - bx : bx - ax;
      if (gegen > weit_d) { weit_d = gegen; weit = (int) i; }
      const bool richtig = dx > 0 ? b.x >= a.x + a.w : b.x + b.w <= a.x;
      if (!richtig) continue;
      haupt = std::abs(bx - ax);
      neben = std::abs(by - ay);
    }
    const float d = haupt + neben * 2.0f;
    if (d < best_d) { best_d = d; best = (int) i; }
  }
  if (best >= 0) fokus_setzen_(best);
  else if (dy != 0) fokus_setzen_(FOKUS_ZURUECK);          // oben/unten raus: X/Zurueck
  else if (weit >= 0 && weit_d > 0) fokus_setzen_(weit);   // Zeile umlaufen
}

void Menue::ok() {
  if (fokus_ == FOKUS_ZURUECK) { if (zurueck_taste) zurueck_taste(); return; }
  if (fokus_ >= 0 && fokus_ < (int) items_.size()) ausloesen_(items_[fokus_]);
}

}  // namespace menue
}  // namespace esphome
