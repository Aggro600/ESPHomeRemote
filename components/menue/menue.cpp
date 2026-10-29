#include "menue.h"
#include "esphome/components/tasten_konfig/tasten_konfig.h"
#include "mdi_icons.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <set>
#include <strings.h>

namespace esphome {
namespace menue {

static const char *const TAG = "menue";

// Raster und Farben (wie die eingebauten Menueseiten)
// MAX_ZEILEN 256 statt 128 (2026-10-06): "Energie & Wecken" ist mit den Hinweistexten > 128 Zeilen lang - die letzten
// Eintraege lagen ausserhalb des Rasters, alle uebereinander und unsichtbar: Fokus verschwand, unten nie erreichbar.
static const int SPALTEN = 6, ZEILE = 40, RAND = 6, LUECKE = 4, MAX_ZEILEN = 256;
// Rand oben nur 2 px (2026-10-12): die Kopfzeile ist 4 px hoeher, damit das X nicht am oberen Displayrand klebt -
// die Karten stehen auf dem Display an derselben Stelle wie vorher, alle 7 Reihen passen weiter.
static const int RAND_O = 2;
static const uint32_t C_KARTE = 0x1A1F27, C_FOKUS = 0x2A3340, C_RAHMEN_F = 0x3B82F6;
static int32_t farbe_von(const char *s);
// Hervorhebung des geladenen Menues (menu.hl) - auch fuer die freien Hilfsfunktionen unten (Regler)
static uint32_t g_hl = C_RAHMEN_F;
static const uint32_t C_TEXT = 0xF1F3F5, C_GRAU = 0x8B94A3, C_AN = 0x22C55E;

// LVGL 9.5: jedes Label mit neuem Text haengt einen Rueckruf ans Display (LV_EVENT_UPDATE_LAYOUT_COMPLETED),
// erst der naechste Layout-Durchlauf entfernt ihn. Beim Seitenaufbau wird die Liste lang und JEDES Display-
// Ereignis (bei jeder Stil-Aenderung) laeuft sie ab -> quadratisch. Wie lv_label_rueckrufe_leeren() beim Start
// (esphomeremote-wlan.h): nach jeder Karte abarbeiten (2026-10-06, Energie & Wecken).
static void rueckrufe_leeren_() {
  lv_display_t *d = lv_display_get_default();
  if (d != nullptr) lv_display_send_event(d, LV_EVENT_UPDATE_LAYOUT_COMPLETED, nullptr);
}


// Weiches Auslaufen (ease-out cubic) fuer Einblendungen: 1 - (1 - t)^3, Werte 0..1024
static int32_t ease_out_kubisch(const lv_anim_t *an) {
  const int64_t r = 1024 - (lv_anim_path_linear(an) - an->start_value);
  return an->start_value + (int32_t) (1024 - r * r * r / (1024 * 1024));
}

// Deckkraft der Scrollleiste (Ausblende-Animation)
static void leiste_opa_(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *) o, (lv_opa_t) v, LV_PART_SCROLLBAR); }

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
  if (!strcmp(t, "number")) return T_ZAHL;
  if (!strcmp(t, "select")) return T_AUSWAHL;
  if (!strcmp(t, "mtitel") || !strcmp(t, "mtaste") || !strcmp(t, "vpreset")) return T_PLAYER;
  if (!strcmp(t, "widget")) return T_WIDGET;
  return T_TEXT;
}

const char *Menue::icon_(const char *name) {
  static char buf[5];
  if (name == nullptr || *name == 0) return nullptr;
  for (const auto &e : MDI_ICONS) {
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
    // Die alte Belegung ist hier schon freigegeben - alle Eintraege zeigen (Item::j) noch hinein. Nichts davon darf
    // mehr gelesen werden (2026-10-07, Absturz LoadProhibited beim Neuladen nach HA-Neustart): Halte-Knopf ohne
    // "release" vergessen, Eintraege und Widgets sofort weg, auch wenn das Menue gerade nicht sichtbar ist.
    halt_ = -1;
    tmodus_ = false;
    hoehen_neu_ = false;
    if (fokus_ == FOKUS_ZURUECK) fokus_setzen_(-1);   // X-Knopf wieder normal (beruehrt keine Eintraege)
    fokus_ = -1;
    if (root_ != nullptr) lv_obj_clean(root_);
    items_.clear();
    wartend_.clear();
    wartend_pos_ = 0;
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
  JsonObjectConst m = tk_->wurzel()["menu"];
  an_ = !m.isNull() && m["pages"].size() > 0;   // ohne Konfiguration gibt es kein Menue
  if (m.isNull()) return;
  for (JsonObjectConst p : m["pages"].as<JsonArrayConst>()) {
    const char *id = p["id"] | "";
    if (*id) seiten_.push_back(Seite{PStr(id), p});
  }
  start_ = m["start"] | (seiten_.empty() ? "" : seiten_[0].id.c_str());
  {
    const int32_t hl = farbe_von(m["hl"] | "");
    hl_ = g_hl = hl >= 0 ? (uint32_t) hl : C_RAHMEN_F;
  }
  if (seite_index_(start_) < 0 && !seiten_.empty()) start_ = seiten_[0].id.c_str();
  // Zustaende der Popup-Ausloeser abonnieren (die Kacheln holen ihre Zustaende per HTTP, solange die Seite offen
  // ist - abfrage_*). Neue Entitaeten bekommen ihren Zustand erst beim naechsten Verbinden mit HA: die Abo-Liste
  // zur Laufzeit neu zu senden brachte HAs Einrichtung mitten im Verbindungsaufbau zum Scheitern (2026-10-03).
  for (auto &s : seiten_) {
    if (nur_anzeige_) break;   // Startseite: Popup-Ausloeser macht das Menue
    for (const char *art : {"open", "close", "if"})
      for (JsonObjectConst a : s.j["popup"][art].as<JsonArrayConst>()) {
        const char *e = a["entity"] | "";
        const char *at = a["attr"] | "";
        if (*e) abonnieren_(e, *at ? at : nullptr);
      }
  }
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
      entity, attr ? optional<std::string>(a) : optional<std::string>(),
      [this, entity, a](StringRef s) { zustand_neu_(entity, a, std::string(s.c_str(), s.size())); });
#endif
}

void Menue::zustand_neu_(const std::string &entity, const std::string &a, const std::string &s) {
  EntZustand &z = zustand_cache_[PStr(entity.c_str())];
  std::string alt;
  if (a.empty()) { alt = z.state.c_str(); z.state.assign(s.c_str(), s.size()); }
  else if (a == "brightness") z.bri = s.size() ? atoi(s.c_str()) : -1;
  else if (a == "current_position") z.pos = s.size() ? atoi(s.c_str()) : -1;
  else if (a == "unit_of_measurement") z.unit.assign(s.c_str(), s.size());
  if (!a.empty()) { PStr ak(a.c_str()); auto f = z.attr.find(ak); if (f != z.attr.end()) alt = f->second.c_str(); z.attr[ak].assign(s.c_str(), s.size()); }
  aktualisieren_(entity);
  pegel_pruefen_(entity, a, s);   // Tastatur & Co.: nach dem Zustand, auch der erste Wert
  // Der erste Wert nach dem Start ist kein Wechsel (sonst ginge z. B. der Sleeptimer bei jedem
  // Aufwachen neu auf); spaetere Wiederholungen beim Neuverbinden aendern nichts (alt == neu).
  static std::set<std::string> bekannt;
  if (bekannt.insert(entity + "|" + a).second) return;
  ausloeser_(entity, a, alt, s);
}

// Popup-Ausloeser ueber das Dock (2026-10-24, Umbau "Dock als Zentrale"): ohne eigene HA-Verbindung liefert HA die
// Zustaende im Dock-Stand mit ("pz", zustand.py) - als Liste in derselben Reihenfolge wie hier (Seiten, dann open/
// close/if, doppelte weg). null = unbekannt. Danach wie ein Wert von der API (erster Wert ist kein Wechsel).
void Menue::zustaende_von_dock(JsonArrayConst werte) {
  std::vector<std::string> reihe;
  for (auto &s : seiten_)
    for (const char *art : {"open", "close", "if"})
      for (JsonObjectConst a : s.j["popup"][art].as<JsonArrayConst>()) {
        const char *e = a["entity"] | "";
        if (!*e) continue;
        const std::string key = std::string(e) + "|" + (a["attr"] | "");
        if (std::find(reihe.begin(), reihe.end(), key) == reihe.end()) reihe.push_back(key);
      }
  if (reihe.size() != werte.size()) { ESP_LOGW(TAG, "Popup-Zustaende vom Dock: %u statt %u", (unsigned) werte.size(), (unsigned) reihe.size()); return; }
  for (size_t i = 0; i < reihe.size(); i++) {
    if (werte[i].isNull()) continue;
    const size_t p = reihe[i].find('|');
    zustand_neu_(reihe[i].substr(0, p), reihe[i].substr(p + 1), werte[i].as<std::string>());
  }
}

// Zustandswechsel -> Popup oeffnen/schliessen.
void Menue::ausloeser_(const std::string &entity, const std::string &attr, const std::string &alt, const std::string &neu) {
  if (alt == neu) return;
  for (auto &s : seiten_) {
    JsonObjectConst pp = s.j["popup"];
    if (pp.isNull() || *(s.j["inhalt"] | "")) continue;   // eingebauter Inhalt: pegel_pruefen_
    auto passt = [&](JsonObjectConst a) {
      return entity == (a["entity"] | "") && attr == (a["attr"] | "") && strcasecmp(neu.c_str(), a["to"] | "") == 0;
    };
    for (JsonObjectConst a : pp["close"].as<JsonArrayConst>())
      if (passt(a)) { ESP_LOGI(TAG, "Popup '%s' schliessen (%s -> %s)", s.id.c_str(), entity.c_str(), neu.c_str()); popup_ausloesen_(s, false, neu); break; }
    for (JsonObjectConst a : pp["open"].as<JsonArrayConst>())
      if (passt(a)) { ESP_LOGI(TAG, "Popup '%s' oeffnen (%s -> %s)", s.id.c_str(), entity.c_str(), neu.c_str()); popup_ausloesen_(s, true, neu); break; }
  }
}

// Popups mit eingebautem Inhalt (Tastatur, 2026-10-26) folgen dem Zustand: Oeffnen-Wert und noch nicht offen -> auf,
// Schliessen-Wert -> zu. Vorher nur beim Wechsel: verpasste die Remote das Zugehen (lag ruhig, fragte das Dock nicht),
// war das naechste Aufgehen fuer sie kein Wechsel - die Tastatur kam nicht wieder. Derselbe Oeffnen-Wert erneut:
// inhalt_noch (weggelegte Tastatur nach Ablauf wieder zeigen).
void Menue::pegel_pruefen_(const std::string &entity, const std::string &attr, const std::string &neu) {
  for (auto &s : seiten_) {
    const char *inhalt = s.j["inhalt"] | "";
    JsonObjectConst pp = s.j["popup"];
    if (!*inhalt || pp.isNull()) continue;
    auto passt = [&](JsonObjectConst a) {
      return entity == (a["entity"] | "") && attr == (a["attr"] | "") && strcasecmp(neu.c_str(), a["to"] | "") == 0;
    };
    const std::string id = s.id.c_str();
    for (JsonObjectConst a : pp["close"].as<JsonArrayConst>())
      if (passt(a)) {
        auto f = pegel_.find(id);
        if (f == pegel_.end() || f->second != neu) {
          ESP_LOGI(TAG, "Popup '%s' zu (%s = %s)", id.c_str(), entity.c_str(), neu.c_str());
          pegel_[id] = neu;
          popup_ausloesen_(s, false, neu);
        }
        break;
      }
    for (JsonObjectConst a : pp["open"].as<JsonArrayConst>())
      if (passt(a)) {
        auto f = pegel_.find(id);
        if (f == pegel_.end() || f->second != neu) {
          ESP_LOGI(TAG, "Popup '%s' auf (%s = %s)", id.c_str(), entity.c_str(), neu.c_str());
          pegel_[id] = neu;
          popup_ausloesen_(s, true, neu);
        } else if (inhalt_noch) {
          inhalt_noch(inhalt, neu);
        }
        break;
      }
  }
}

void Menue::ereignis(const std::string &art, const std::string &wert) {
  for (auto &s : seiten_) {
    JsonObjectConst pp = s.j["popup"];
    if (pp.isNull()) continue;
    auto passt = [&](JsonObjectConst a) { return art == (a["ev"] | "") && strcasecmp(wert.c_str(), a["to"] | "") == 0; };
    for (JsonObjectConst a : pp["close"].as<JsonArrayConst>())
      if (passt(a)) { popup_ausloesen_(s, false, wert); break; }
    for (JsonObjectConst a : pp["open"].as<JsonArrayConst>())
      if (passt(a)) { ESP_LOGI(TAG, "Popup '%s' oeffnen (Ereignis %s = %s)", s.id.c_str(), art.c_str(), wert.c_str()); popup_ausloesen_(s, true, wert); break; }
  }
}

// "Nur wenn" (2026-10-13): alle Bedingungen muessen gelten, Werte aus den abonnierten Zustaenden
bool Menue::bedingungen_ok_(JsonObjectConst pp) {
  for (JsonObjectConst c : pp["if"].as<JsonArrayConst>()) {
    const char *e = c["entity"] | "", *at = c["attr"] | "";
    std::string ist;
    auto f = zustand_cache_.find(PStr(e));
    if (f != zustand_cache_.end()) {
      if (!*at) ist = f->second.state.c_str();
      else { auto g = f->second.attr.find(PStr(at)); if (g != f->second.attr.end()) ist = g->second.c_str(); }
    }
    bool gleich = strcasecmp(ist.c_str(), c["to"] | "") == 0;
    if (c["neg"] | false) gleich = !gleich;
    if (!gleich) { ESP_LOGI(TAG, "Bedingung %s%s%s nicht erfuellt (ist '%s')", e, *at ? "." : "", at, ist.c_str()); return false; }
  }
  return true;
}

bool Menue::exklusiv_offen_(const std::string &ausser) const {
  if (!popup_ || popup_id_ == ausser || root_ == nullptr || lv_screen_active() != lv_obj_get_screen(root_)) return false;
  const int si = seite_index_(popup_id_);
  return si >= 0 && (seiten_[si].j["popup"]["excl"] | false);
}

void Menue::popup_ausloesen_(const Seite &s, bool auf, const std::string &wert) {
  JsonObjectConst pp = s.j["popup"];
  const std::string id = s.id.c_str(), inhalt = s.j["inhalt"] | "";
  if (!auf) {
    for (size_t i = 0; i < warten_.size(); i++)   // wartet es noch: erledigt
      if (warten_[i].first == id) { warten_.erase(warten_.begin() + i); break; }
    if (!inhalt.empty()) { if (inhalt_zu) inhalt_zu(inhalt); }
    else if (popup_zu) popup_zu(id);
    return;
  }
  if (!bedingungen_ok_(pp)) return;
  if (exklusiv_offen_(id)) {
    ESP_LOGI(TAG, "Popup '%s' wartet: '%s' ist offen und laesst nichts darueber", id.c_str(), popup_id_.c_str());
    for (auto &w : warten_) if (w.first == id) { w.second = wert; return; }
    warten_.push_back({id, wert});
    return;
  }
  const bool wecken = pp["wake"] | true;
  if (!inhalt.empty()) inhalt_knopf_min_ = !strcmp(s.j["pknopf"] | "", "min");
  if (!inhalt.empty()) { if (inhalt_auf) inhalt_auf(inhalt, wert, wecken); }
  else if (popup_auf) popup_auf(id, wecken);
  const uint32_t sek = pp["timeout"] | 0;
  if (sek > 0) {
    set_timeout("popup_zeit", sek * 1000, [this, id, inhalt]() {
      if (!inhalt.empty()) { if (inhalt_zu) inhalt_zu(inhalt); }
      else if (popup_ && popup_id_ == id && popup_zu) popup_zu(id);
    });
  } else {
    cancel_timeout("popup_zeit");
  }
}

// ------------------------------------------------------------------ Zustaende per HTTP

void Menue::loop() {
  // Wartende Popups: sobald das exklusive zu ist (kurz danach, nach der Schliess-Animation) das naechste oeffnen
  if (!warten_.empty()) {
    if (exklusiv_offen_("")) warten_ab_ = millis();
    else if (millis() - warten_ab_ > 600) {
      const auto w = warten_.front();
      warten_.erase(warten_.begin());
      const int si = seite_index_(w.first);
      if (si >= 0) { ESP_LOGI(TAG, "Wartendes Popup '%s' oeffnen", w.first.c_str()); popup_ausloesen_(seiten_[si], true, w.second); }
      warten_ab_ = millis();
    }
  }
  bau_weiter_();
  if (leiste_bis_ != 0 && (int32_t) (millis() - leiste_bis_) >= 0 && root_ != nullptr) {
    leiste_bis_ = 0;
    lv_anim_t a;   // sanft ausblenden (300 ms)
    lv_anim_init(&a);
    lv_anim_set_var(&a, root_);
    lv_anim_set_exec_cb(&a, leiste_opa_);
    lv_anim_set_values(&a, lv_obj_get_style_bg_opa(root_, LV_PART_SCROLLBAR), 0);
    lv_anim_set_duration(&a, 300);
    lv_anim_start(&a);
  }
  if (abf_fertig_) { abf_fertig_ = false; abf_laeuft_ = false; abfrage_auswerten_(); }
  const bool sichtbar = root_ != nullptr && !items_.empty() && lv_screen_active() == lv_obj_get_screen(root_);
  if (hoehen_neu_) hoehen_jetzt_();
  if (sichtbar && millis() - fortschritt_ms_ >= 1000) {   // Fortschrittsbalken der Titel-Kacheln
    fortschritt_ms_ = millis();
    for (auto &it : items_) if (it.teil == P_INFO && (it.j["bar"] | false)) player_zustand_(it);
  }
  if (sichtbar && millis() - wtick_ms_ >= 1000) { wtick_ms_ = millis(); widget_tick_(); }
  if (sichtbar && (int32_t) (millis() - einst_ab_) >= 0) {
    einst_ab_ = millis() + 700;
    for (auto &it : items_) if (it.typ == T_EINST) einst_zeigen_(it);
  }
  if (!sichtbar || abf_laeuft_ || (int32_t) (millis() - abf_ab_) < 0) return;
  abfrage_starten_();
}

void Menue::abfrage_starten_() {
  std::string liste, attrs;
  auto dazu = [&](const char *e) {
    if (!*e || liste.find(std::string(e) + ",") != std::string::npos) return;
    liste += e;
    liste += ",";
  };
  for (auto &it : items_) {
    const char *e = it.j["entity"] | "";
    const bool extras = !it.j["zst"].isNull() || !it.j["map"].isNull();
    if (it.wert != nullptr || it.teil != P_KEIN || extras || it.typ == T_WIDGET) dazu(e);
    if (it.typ == T_WIDGET) {
      dazu(it.j["we"] | "");
      dazu(it.j["me"] | "");
      for (JsonObjectConst l : it.j["lichter"].as<JsonArrayConst>()) dazu(l["e"] | "");   // Licht-Widget
      dazu(it.j["gruppe"] | "");
    }
    // Quellen fuer zweite Zeile, Abzeichen und Bedingung (2026-10-26): andere Entitaet bzw. Attribut ("e:a")
    for (const char *q : {"sub", "badge", "wenn"}) {
      JsonObjectConst o = it.j[q];
      if (o.isNull() || !o["t"].isNull()) continue;
      const char *qe = o["e"] | e, *qa = o["a"] | "";
      if (!*qe) continue;
      if (!*qa) { dazu(qe); continue; }
      const std::string key = std::string(qe) + ":" + qa + ",";
      if (attrs.find(key) == std::string::npos) attrs += key;
    }
  }
  for (auto &e : weg_e_) dazu(e.c_str());   // ausgeblendete Medien-Widgets: kommt die Wiedergabe?
  if (!attrs.empty()) liste += "&a=" + attrs;
  abf_ab_ = millis() + 5000;   // alle 5 s, solange die Seite offen ist
  if (liste.empty()) return;
  abf_url_ = "http://" + tk_->host() + "/api/esphomeremote_konfig/states?k=" + tk_->schluessel() + "&e=" + liste;
  abf_laeuft_ = true;
  abf_fertig_ = false;
  // einmal angelegter Arbeits-Task mit PSRAM-Stapel (sonst alle 5 s 4 KB internes RAM an und ab)
  if (worker_ == nullptr) {
    const size_t groesse = 4096;
    auto *stapel = (StackType_t *) heap_caps_malloc(groesse, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stapel != nullptr)
      worker_ = xTaskCreateStaticPinnedToCore(abfrage_task_, nur_anzeige_ ? "mn_home" : "mn_abf", groesse, this, 1, stapel,
                                              &abf_tcb_, tskNO_AFFINITY);
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
  // Sendepuffer nach der URL (2026-10-29): Licht-Widgets fragen viele Lampen ab - mit 512 Byte "Out of buffer"
  cfg.buffer_size_tx = std::max<int>(512, (int) self->abf_url_.size() + 256);
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
        const char *key = kv.key().c_str(), *dp = strchr(key, ':');
        if (dp != nullptr) {   // Attribut einer Kachel-Quelle: "entity:attr": ["Wert"]
          EntZustand &za = zustand_cache_[PStr(key, dp - key)];
          za.attr[PStr(dp + 1)] = kv.value()[0] | "";
          continue;
        }
        EntZustand &z = zustand_cache_[PStr(key)];
        JsonArrayConst a = kv.value();
        z.state = a[0] | "";
        z.bri = a[1] | -1;
        z.pos = a[2] | -1;
        z.unit = a[3] | "";
        if (strncmp(kv.key().c_str(), "media_player.", 13) == 0) {
          // Medien-Steuerung (2026-10-26): Titel, Interpret/App, stumm
          if (a.size() >= 7) {
            z.attr[PStr("media_title")] = a[4] | "";
            z.attr[PStr("media_artist")] = a[5] | "";
            z.attr[PStr("is_volume_muted")] = (a[6] | false) ? "true" : "false";
          }
          if (a.size() >= 9) {
            z.attr[PStr("shuffle")] = (a[7] | false) ? "true" : "false";
            z.attr[PStr("repeat")] = a[8] | "";
          }
          if (a.size() >= 14) {   // App, Quelle, Position/Dauer (s) und Alter der Position (s) - Fortschrittsbalken
            z.attr[PStr("app_name")] = a[9] | "";
            z.attr[PStr("source")] = a[10] | "";
            char t[16];
            snprintf(t, sizeof(t), "%.1f", (float) (a[11] | -1.0f));
            z.attr[PStr("media_position")] = t;
            snprintf(t, sizeof(t), "%.0f", (float) (a[12] | 0.0f));
            z.attr[PStr("media_duration")] = t;
            z.pos_ms = millis() - (uint32_t) ((a[13] | 0.0f) * 1000.0f);
          }
        } else if (strncmp(key, "weather.", 8) == 0) {
          // Widget Wetter: Temperatur, Einheit, Feuchte, Wind, Windeinheit, Vorhersage [[Tag, Zustand, max, min]]
          char t[16];
          auto zahl = [&](JsonVariantConst v, const char *n, int nk) {
            if (v.isNull()) { z.attr[PStr(n)] = ""; return; }
            snprintf(t, sizeof(t), nk ? "%.1f" : "%.0f", v.as<float>());
            z.attr[PStr(n)] = t;
          };
          zahl(a[4], "temperature", 1);
          z.attr[PStr("temperature_unit")] = a[5] | "°C";
          zahl(a[6], "humidity", 0);
          zahl(a[7], "wind_speed", 0);
          z.attr[PStr("wind_unit")] = a[8] | "";
          std::string fc;   // "Mo|rainy|12|5;Di|..."
          for (JsonArrayConst f : a[9].as<JsonArrayConst>()) {
            if (!fc.empty()) fc += ";";
            char b[48];
            snprintf(b, sizeof(b), "%s|%s|%.0f|%.0f", f[0] | "", f[1] | "", f[2].isNull() ? -99.0f : f[2].as<float>(),
                     f[3].isNull() ? -99.0f : f[3].as<float>());
            fc += b;
          }
          z.attr[PStr("fc")] = fc.c_str();
        } else if (strncmp(key, "timer.", 6) == 0) {
          // Widget Timer: Restzeit (s), Dauer (s), Name - die Remote zaehlt ab pos_ms selbst weiter
          char t[16];
          snprintf(t, sizeof(t), "%d", (int) (a[4] | 0));
          z.attr[PStr("rest")] = t;
          snprintf(t, sizeof(t), "%d", (int) (a[5] | 0));
          z.attr[PStr("dauer")] = t;
          z.attr[PStr("name")] = a[6] | "";
          z.pos_ms = millis();
        } else if (strncmp(key, "light.", 6) == 0 && a.size() >= 12) {
          // Licht-Widget (2026-10-29): Farbmodus, Farbton, Saettigung, Kelvin, Kelvin min/max, Faehigkeiten, Name
          char t[12];
          z.attr[PStr("cm")] = a[4] | "";
          for (int k = 5; k <= 10; k++) {
            static const char *const N[] = {"h", "s", "k", "kmin", "kmax", "caps"};
            const int w = a[k] | -1;
            if (w < 0) z.attr[PStr(N[k - 5])] = "";
            else { snprintf(t, sizeof(t), "%d", w); z.attr[PStr(N[k - 5])] = t; }
          }
          z.attr[PStr("name")] = a[11] | "";
        } else if (a.size() >= 7) { z.mn = a[4] | 0.0f; z.mx = a[5] | 100.0f; z.schritt = a[6] | 1.0f; }
      }
      for (auto &it : items_) zustand_(it);
      // Medien-Widgets mit "leer: weg": Wiedergabe begonnen/beendet -> Seite neu aufbauen (rueckt zusammen/auf)
      bool neu = false;
      for (auto &e : weg_e_) neu |= medien_laeuft_(e.c_str());
      for (auto &it : items_) if (it.typ == T_WIDGET && widget_weg_(it.j)) neu = true;
      if (neu && !stapel_.empty()) { ESP_LOGI(TAG, "Medien-Widget ein-/ausgeblendet - Seite neu"); bauen_(); }
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

// Karten nacheinander von oben nach unten einblenden (2026-10-13, Nutzerwunsch "edel wie Apple"): jede Karte
// gleitet 12 px von unten hoch und blendet dabei weich auf; Zeilen im Abstand von 60 ms, Karten derselben
// Zeile 5 ms versetzt. ab = Start (ms) nach dem Aufruf, z. B. nach der Kopf-Animation.
void Menue::einblenden(uint32_t ab) {
  einbl_t0_ = millis();
  einbl_ab_ = ab;
  einbl_bis_ = einbl_t0_ + (ab + 1500) * zeitlupe;   // spaeter (in Haeppchen) gebaute Karten blenden mit ein
  for (auto &it : items_) einbl_item_(it);
  titel_einbl_();   // Titel erscheint wie die Karten (Nutzerwunsch 2026-10-13: nicht einfliegen)
}

// Titel wie eine Karte der ersten Zeile einblenden: weich aufblenden, 12 px von unten. Die Kopfzeile verschiebt
// den Titel selbst per translate_y (mittig zum Knopf) - darauf aufsetzen und am Ende wiederherstellen.
void Menue::titel_einbl_() {
  if (titel_ == nullptr) return;
  titel_weg_();
  lv_anim_delete(titel_, nullptr);
  titel_ty_ = lv_obj_get_style_translate_y(titel_, LV_PART_MAIN);
  titel_menue_ = this;
  lv_obj_set_style_opa(titel_, LV_OPA_TRANSP, 0);
  if (zurueck_lbl_ != nullptr) lv_obj_set_style_opa(zurueck_lbl_, LV_OPA_TRANSP, 0);   // X kommt mit dem Titel
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, titel_);
  lv_anim_set_delay(&a, einbl_ab_ * zeitlupe);
  lv_anim_set_duration(&a, 380 * zeitlupe);
  lv_anim_set_values(&a, 0, 1024);
  lv_anim_set_path_cb(&a, ease_out_kubisch);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t p) {
    lv_obj_set_style_opa((lv_obj_t *) v, (lv_opa_t) std::min<int32_t>(255, p * 255 * 3 / 2048), 0);
    lv_obj_set_style_translate_y((lv_obj_t *) v, titel_menue_->titel_ty_ + 12 - 12 * p / 1024, 0);
    if (lv_obj_t *x = titel_menue_->zurueck_lbl_) {
      lv_obj_set_style_opa(x, (lv_opa_t) std::min<int32_t>(255, p * 255 * 3 / 2048), 0);
      lv_obj_set_style_translate_y(x, 12 - 12 * p / 1024, 0);
    }
  });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) {
    lv_obj_remove_local_style_prop((lv_obj_t *) an->var, LV_STYLE_OPA, 0);
    lv_obj_set_style_translate_y((lv_obj_t *) an->var, titel_menue_->titel_ty_, 0);
    if (lv_obj_t *x = titel_menue_->zurueck_lbl_) {
      lv_obj_remove_local_style_prop(x, LV_STYLE_OPA, 0);
      lv_obj_remove_local_style_prop(x, LV_STYLE_TRANSLATE_Y, 0);
    }
  });
  lv_anim_start(&a);
}

void Menue::einbl_item_(Item &it) {
  static const int HUB = 12;
  lv_obj_t *o = it.obj;
  if (o == nullptr) return;
  // Welle ab der obersten sichtbaren Zeile (2026-10-14): wer unten weitermacht, wartete sonst, bis die Welle
  // unsichtbar durch alle Zeilen darueber gelaufen war. Karten ausserhalb des Bildes kommen ohne Animation.
  const int z0 = std::max<int>(0, (lv_obj_get_scroll_y(root_) - RAND_O) / ZEILE);
  const int z1 = z0 + lv_obj_get_height(root_) / ZEILE + 1;
  if (it.y + it.h <= z0 || it.y > z1) return;
  // Zeitpunkt aus der Rasterzeile: Zeilen 60 ms auseinander, Karten derselben Zeile leicht versetzt
  // Erste Zeile zusammen mit Titel und X (Zeitpunkt ab) - Nutzerwunsch 2026-10-22 "beides gleichzeitig", vorher
  // kamen die Karten erst 250 ms danach (zu traege). Abstand der Zeilen untereinander bleibt.
  const uint32_t ziel = einbl_t0_ + (einbl_ab_ + std::max(0, it.y - z0) * 60 + it.x * 5) * zeitlupe;
  const uint32_t jetzt = millis();
  lv_anim_delete(o, nullptr);
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_translate_y(o, HUB, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_delay(&a, (int32_t) (ziel - jetzt) > 0 ? ziel - jetzt : 0);
  lv_anim_set_duration(&a, 380 * zeitlupe);
  lv_anim_set_values(&a, 0, 1024);
  lv_anim_set_path_cb(&a, ease_out_kubisch);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t p) {
    lv_obj_t *o = (lv_obj_t *) v;
    lv_obj_set_style_opa(o, (lv_opa_t) std::min<int32_t>(255, p * 255 * 3 / 2048), 0);   // Deckkraft etwas frueher voll
    lv_obj_set_style_translate_y(o, HUB - HUB * p / 1024, 0);
  });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) {
    lv_obj_t *o = (lv_obj_t *) an->var;
    lv_obj_remove_local_style_prop(o, LV_STYLE_OPA, 0);
    lv_obj_remove_local_style_prop(o, LV_STYLE_TRANSLATE_Y, 0);
  });
  lv_anim_start(&a);
}

// Laufendes Rutschen sofort beenden: alte Karten loeschen, Inhalt wieder an seinen Platz
void Menue::alt_weg_() {
  lv_anim_delete(this, nullptr);
  if (alt_ != nullptr) { lv_obj_delete(alt_); alt_ = nullptr; }
  if (root_ != nullptr) lv_obj_remove_local_style_prop(root_, LV_STYLE_TRANSLATE_X, 0);
}

// Karten der aktuellen Seite in einen eigenen Behaelter an derselben Stelle umhaengen (gleicher Scrollstand),
// damit sie noch wegrutschen koennen, waehrend root_ schon die neue Seite aufbaut. Nicht mehr bedienbar.
lv_obj_t *Menue::alt_abheben_() {
  alt_weg_();
  if (root_ == nullptr || lv_obj_get_child_count(root_) == 0) return nullptr;
  const int32_t sy = lv_obj_get_scroll_y(root_);
  lv_obj_t *alt = lv_obj_create(lv_obj_get_parent(root_));
  lv_obj_remove_style_all(alt);
  lv_obj_set_pos(alt, lv_obj_get_x(root_), lv_obj_get_y(root_));
  lv_obj_set_size(alt, lv_obj_get_width(root_), lv_obj_get_height(root_));
  lv_obj_set_style_pad_bottom(alt, lv_obj_get_style_pad_bottom(root_, LV_PART_MAIN), 0);
  lv_obj_set_scrollbar_mode(alt, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(alt, LV_OBJ_FLAG_CLICKABLE);
  while (lv_obj_get_child_count(root_) > 0) lv_obj_set_parent(lv_obj_get_child(root_, 0), alt);
  lv_obj_tree_walk(alt, [](lv_obj_t *o, void *) {
    lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return LV_OBJ_TREE_WALK_NEXT;
  }, nullptr);
  lv_obj_add_flag(alt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_update_layout(alt);
  lv_obj_scroll_to_y(alt, sy, LV_ANIM_OFF);
  lv_obj_remove_flag(alt, LV_OBJ_FLAG_SCROLLABLE);   // Wischen waehrend des Rutschens geht an root_
  return alt;
}

void Menue::titel_weg_() {
  if (titel_ != nullptr && lv_anim_get(titel_, nullptr) != nullptr) {   // Einblendung beim Oeffnen: sofort fertig
    lv_anim_delete(titel_, nullptr);
    lv_obj_set_style_translate_y(titel_, titel_ty_, 0);
    if (zurueck_lbl_ != nullptr) {
      lv_obj_remove_local_style_prop(zurueck_lbl_, LV_STYLE_OPA, 0);
      lv_obj_remove_local_style_prop(zurueck_lbl_, LV_STYLE_TRANSLATE_Y, 0);
    }
  }
  if (titel_fenster_ != nullptr) { lv_obj_delete(titel_fenster_); titel_fenster_ = nullptr; }   // loescht auch die Animation
  if (titel_ != nullptr) lv_obj_remove_local_style_prop(titel_, LV_STYLE_OPA, 0);
}

void Menue::titel_anim_(int r, uint32_t verz) {
  if (titel_ == nullptr || zurueck_btn_ == nullptr) return;
  titel_weg_();
  lv_obj_t *hd = lv_obj_get_parent(titel_);
  lv_obj_update_layout(hd);
  lv_area_t k, t, b, in;
  lv_obj_get_coords(hd, &k);
  lv_obj_get_coords(titel_, &t);
  lv_obj_get_coords(zurueck_btn_, &b);   // mit Verschiebung (translate) ins blaue Feld
  lv_obj_get_content_coords(hd, &in);
  const int ab = b.x2 + 1 + 4;            // rechter Rand des blauen Felds (4 px Rand um den Knopf)
  // Fenster: vom blauen Feld bis zum rechten Displayrand, ganze Kopfhoehe ueber der Linie; schneidet ab
  lv_obj_t *f = lv_obj_create(hd);
  lv_obj_remove_style_all(f);
  lv_obj_add_flag(f, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_remove_flag(f, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(f, ab - in.x1, k.y1 - in.y1);
  lv_obj_set_size(f, k.x2 + 1 - ab, t.y2 + 1 - k.y1);
  lv_obj_set_user_data(f, this);
  titel_fenster_ = f;
  titel_r_ = r;
  titel_x0_ = t.x1 - ab;
  titel_weit_ = lv_obj_get_width(lv_obj_get_screen(hd));
  auto kopie = [&](const char *txt) {
    lv_obj_t *l = lv_label_create(f);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, lv_obj_get_style_text_font(titel_, LV_PART_MAIN), 0);
    lv_obj_set_style_text_color(l, lv_obj_get_style_text_color(titel_, LV_PART_MAIN), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_size(l, t.x2 - t.x1 + 1, t.y2 - t.y1 + 1);
    lv_obj_set_pos(l, titel_x0_, t.y1 - k.y1);
    return l;
  };
  kopie(lv_label_get_text(titel_));                    // Kind 0: neuer Titel
  if (!titel_alt_.empty()) kopie(titel_alt_.c_str());   // Kind 1: alter Titel
  titel_alt_.clear();
  lv_obj_set_style_opa(titel_, LV_OPA_TRANSP, 0);      // der echte Titel wartet unsichtbar
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, f);
  lv_anim_set_delay(&a, verz);   // early_apply: neuer Titel sofort draussen
  lv_anim_set_duration(&a, 260 * zeitlupe);
  lv_anim_set_values(&a, 0, 1024);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t p) {
    lv_obj_t *f = (lv_obj_t *) v;
    auto *m = (Menue *) lv_obj_get_user_data(f);
    const int32_t w = m->titel_weit_, r = m->titel_r_;
    lv_obj_set_x(lv_obj_get_child(f, 0), m->titel_x0_ + r * w * (1024 - p) / 1024);
    if (lv_obj_get_child_count(f) > 1) lv_obj_set_x(lv_obj_get_child(f, 1), m->titel_x0_ - r * w * p / 1024);
  });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) {
    auto *m = (Menue *) lv_obj_get_user_data((lv_obj_t *) an->var);
    m->titel_ende_ = (lv_obj_t *) an->var;
    lv_async_call([](void *v) {   // im naechsten Durchlauf - falls inzwischen kein neuer Wechsel kam
      auto *m = (Menue *) v;
      if (m->titel_fenster_ != nullptr && m->titel_fenster_ == m->titel_ende_) m->titel_weg_();
    }, m);
  });
  lv_anim_start(&a);
}

void Menue::rutschen_(int r) {
  titel_anim_(r, 0);
  if (alt_ == nullptr || root_ == nullptr) return;
  rutsch_ = r;   // nur fuer den exec-Rueckruf (Richtung); bauen_ ist schon durch
  const int32_t W = lv_obj_get_width(root_);
  lv_obj_set_style_translate_x(root_, r * W, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, this);
  lv_anim_set_duration(&a, 260 * zeitlupe);
  lv_anim_set_values(&a, 0, W);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t x) {
    auto *m = (Menue *) v;
    const int32_t W = lv_obj_get_width(m->root_);
    if (m->alt_) lv_obj_set_style_translate_x(m->alt_, -m->rutsch_ * x, 0);
    lv_obj_set_style_translate_x(m->root_, m->rutsch_ * (W - x), 0);
  });
  lv_anim_set_completed_cb(&a, [](lv_anim_t *an) {
    // nicht im Ablauf der Animation loeschen: erst im naechsten Durchlauf
    lv_async_call([](void *v) {
      auto *m = (Menue *) v;
      if (lv_anim_get(m, nullptr) != nullptr) return;   // inzwischen schon der naechste Wechsel
      if (m->alt_) { lv_obj_delete(m->alt_); m->alt_ = nullptr; }
      if (m->root_) lv_obj_remove_local_style_prop(m->root_, LV_STYLE_TRANSLATE_X, 0);
      m->rutsch_ = 0;
    }, an->var);
  });
  lv_anim_start(&a);
}

void Menue::spezial_oeffnen(const std::string &name) {
  einstieg_merken_ = true;
  einstieg_ = spezial ? spezial(name) : -1;
}

void Menue::startseite(const std::string &id) {
  start_zeigt_ = id;
  if (id.empty() || seite_index_(id) < 0) { if (!id.empty()) ESP_LOGW(TAG, "Startseite '%s' fehlt", id.c_str()); leeren_(); return; }
  popup_ = false;
  einstieg_ = -1;
  stapel_.clear();
  stapel_.push_back(id);
  bauen_();
}

void Menue::oeffnen_auf(const std::string &id) {
  if (!aktiv()) return;
  popup_ = false;
  einstieg_ = -1;
  stapel_.clear();
  fokus_mem_.clear();
  stapel_.push_back(start_);
  if (id != start_ && seite_index_(id) >= 0) stapel_.push_back(id);
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
  if (stapel_.size() <= 1 || zurueck_x_) return false;   // X: Menue schliessen statt eine Seite zurueck
  fokus_mem_.erase(PStr(stapel_.back().c_str()));  // verlassene Seite vergessen (wie die eingebauten Seiten)
  stapel_.pop_back();
  rutsch_ = -1;
  bauen_();
  rutschen_(-1);
  return true;
}

void Menue::verlassen() {
  titel_weg_();
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
  alt_weg_();
  if (root_ != nullptr) lv_obj_clean(root_);
  cancel_timeout("vol_wdh");   // Lauter/Leiser gehalten (Medien-Steuerung): nicht weiterlaufen lassen
  cancel_interval("vol_wdh");
  items_.clear();
  wartend_.clear();
  wartend_pos_ = 0;
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

// Wert, wie er gerade in der Zeile stuende (fuer die Breitenrechnung)
const char *Menue::wert_text_(const Item &it) const {
  static std::string b;
  if (it.typ == T_EINST) return it.einst.wert != nullptr ? lv_label_get_text(it.einst.wert) : "";
  if (it.typ == T_TOGGLE || it.typ == T_LIGHT || it.typ == T_COVER || it.typ == T_ZAHL) return "an · 100 %";
  if (it.typ != T_SENSOR && it.typ != T_AUSWAHL) return "";
  auto f = zustand_cache_.find(PStr(it.j["entity"] | ""));
  if (f == zustand_cache_.end() || f->second.state.empty()) return "–";
  b = f->second.state.c_str();
  if (!f->second.unit.empty()) { b += " "; b += f->second.unit.c_str(); }
  return b.c_str();
}

// Rasterzeilen eines Eintrags (2026-10-07, Nutzer: "BLE-Verbindung langsam" u. a. wurden mit "..." gekuerzt).
// h aus dem Konfigurator ist das Minimum. Passen Name und Wert nicht nebeneinander in eine Zeile, wird gestapelt:
// Name oben (bricht um), Wert darunter - so viele Zeilen wie noetig.
int Menue::zeilen_(const Item &it, float zelle, bool *stapeln) const {
  *stapeln = false;
  const int h0 = (int) (it.j["h"] | 1);
  if (h0 <= 0) return auto_zeilen_(it, zelle);
  const int h = std::max(1, std::min(8, h0));
  if (*(it.j["lay"] | "")) return h;   // eigene Anordnung (Konfigurator): Hoehe wie eingestellt
  // Feste Anordnung (x/y aus dem Konfigurator): nie selbst wachsen - die Karte ueberdeckte sonst die naechste Reihe
  // und die D-Pad-Navigation sprang ueber sie hinweg ("Größe auf dem TV", "Position" im Kamera-Popup, 2026-10-11).
  // Zu lange Namen bekommen dort kleinere Schrift bzw. "...".
  if (!flow_ && it.typ != T_TEXT) return h;
  const char *label = it.j["label"] | "";
  const int breite = (int) (it.w * zelle) - LUECKE;
  if (it.typ == T_KAMERA || it.typ == T_PLAYER || it.typ == T_WIDGET) return h;
  if (it.typ == T_TEXT) {   // Ueberschrift: umbrechen statt kuerzen
    if (h > 1 || !*label) return h;
    lv_point_t g;
    lv_text_get_size(&g, label, f_klein_, 0, 0, breite - 4, LV_TEXT_FLAG_NONE);
    return std::max(h, std::min(8, (int) ((g.y + 6 + ZEILE - 1) / ZEILE)));
  }
  if (it.typ == T_EINST && it.einst.typ == 'g') return std::max(h, 2);
  if (it.typ == T_EINST && it.einst.typ == 'i' && !*label) return std::max(h, auto_zeilen_(it, zelle));   // Hinweis
  const char *ic = it.j["icon"] | "";
  const bool regler = (it.typ == T_EINST && it.einst.typ == 's') || it.typ == T_LIGHT || it.typ == T_COVER || it.typ == T_ZAHL;
  if (it.w <= 2) return h;                                    // kleine Kachel
  if (h >= 2 && (regler || *ic || it.w < 4)) return h;        // Regler-Karte bzw. Kachel mit Symbol
  const bool mit_wert = it.typ == T_TOGGLE || it.typ == T_LIGHT || it.typ == T_COVER || it.typ == T_SENSOR ||
                        it.typ == T_ZAHL || it.typ == T_AUSWAHL || (it.typ == T_EINST && it.einst.wert != nullptr);
  const int frei = breite - 2 * 10 - (*ic ? 26 : 0) - ((it.typ == T_PAGE || it.typ == T_SPECIAL) ? 14 : 0);
  const char *wert = mit_wert ? wert_text_(it) : "";
  lv_point_t n, w{0, 0};
  lv_text_get_size(&n, label, f_text_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  if (*wert) lv_text_get_size(&w, wert, f_klein_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  if (h == 1 && n.x + (*wert ? w.x + 8 : 0) <= frei) return 1;
  // Schmaler Knopf ohne Wert (z. B. TV | Chromecast nebeneinander): kleinere Schrift statt zweiter Zeile, damit
  // Knoepfe in einer Reihe gleich hoch bleiben (2026-10-07)
  if (h == 1 && !*wert && it.w < 4) {
    lv_text_get_size(&n, label, f_klein_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (n.x <= frei) return 1;
  }
  // Mit kleinerer Schrift passt der Name neben den Wert (karte_ nimmt sie dann): einzeilig bleiben (2026-10-11)
  if (h == 1 && *wert) {
    lv_text_get_size(&n, label, f_klein_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (n.x + w.x + 8 <= frei) return 1;
  }
  *stapeln = true;
  lv_text_get_size(&n, label, f_text_, 0, 0, frei, LV_TEXT_FLAG_NONE);
  int hoehe = *label ? n.y : 0;
  if (*wert) { lv_text_get_size(&w, wert, f_klein_, 0, 0, frei, LV_TEXT_FLAG_NONE); hoehe += (*label ? 2 : 0) + w.y; }
  return std::max(h, std::min(8, (hoehe + 12 + ZEILE - 1) / ZEILE));
}

void Menue::leiste_einrichten_() {
  if (leiste_ok_ || root_ == nullptr) return;
  leiste_ok_ = true;
  // Immer "an", sichtbar nur ueber die Deckkraft: LVGLs Modus "aktiv" zeigt die Leiste nur beim Wischen,
  // nicht beim Scrollen per Programm (D-Pad -> scroll_to_view)
  lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_ON);
  // Nur senkrecht: die Karten ragen ein paar Pixel ueber den Rand (Rundung), das gab eine waagrechte Leiste
  lv_obj_set_scroll_dir(root_, LV_DIR_VER);
  lv_obj_set_style_width(root_, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(root_, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(root_, lv_color_hex(C_GRAU), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(root_, 1, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_ver(root_, 4, LV_PART_SCROLLBAR);
  lv_obj_add_event_cb(root_, [](lv_event_t *e) { ((Menue *) lv_event_get_user_data(e))->leiste_zeigen_(); },
                      LV_EVENT_SCROLL, this);
}

void Menue::leiste_zeigen_() {
  if (items_.empty()) return;   // Seitenaufbau (scroll_to_y 0) zaehlt nicht
  lv_anim_delete(root_, leiste_opa_);
  if (lv_obj_get_style_bg_opa(root_, LV_PART_SCROLLBAR) != 170) lv_obj_set_style_bg_opa(root_, 170, LV_PART_SCROLLBAR);
  leiste_bis_ = millis() + 900;
}

static int32_t farbe_von(const char *s);

void Menue::bauen_(bool alles) {
  if (root_ == nullptr || stapel_.empty()) return;
  const int si = seite_index_(stapel_.back());
  if (si < 0) return;
  einbl_bis_ = 0;   // neue Seite: keine Einblendung mehr (nur beim Oeffnen, einblenden())
  leiste_einrichten_();
  // X/Zurueck: LVGL merkt sich ein angetipptes Element als "fokussiert" (blauer Rahmen) - die Seite wird
  // wiederverwendet, der Rahmen blieb stehen. Markiert wird das X nur ueber fokus_setzen_ (D-Pad).
  if (zurueck_btn_ != nullptr) {
    lv_obj_remove_flag(zurueck_btn_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_state(zurueck_btn_, (lv_state_t) (LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
  }
  if (rutsch_ != 0 && titel_ != nullptr) titel_alt_ = lv_label_get_text(titel_);
  lv_obj_t *alt = rutsch_ != 0 ? alt_abheben_() : nullptr;   // vor leeren_: Karten der alten Seite behalten
  rutsch_ = 0;
  leeren_();
  alt_ = alt;
  lv_anim_delete(root_, leiste_opa_);
  lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
  leiste_bis_ = 0;
  const Seite &s = seiten_[si];
  if (titel_ != nullptr) lv_label_set_text(titel_, s.j["title"] | s.id.c_str());
  {
    // Zurueck/X (2026-10-13): Symbol und Farbe pro Seite. bsym "x" = X (schliesst), "back" = <, sonst automatisch
    const char *bs = s.j["bsym"] | "";
    zurueck_x_ = !strcmp(bs, "x") || (!*bs && stapel_.size() <= 1);
    // Popup mit Minimieren-Knopf (2026-10-27): "_" wie bei Windows
    const bool min = popup_ && stapel_.size() <= 1 && !strcmp(s.j["pknopf"] | "", "min");
    if (zurueck_lbl_ != nullptr) lv_label_set_text(zurueck_lbl_, min ? "_" : (zurueck_x_ ? "X" : "<"));
    const int32_t bc = farbe_von(s.j["bc"] | "");
    zurueck_bg_ = bc >= 0 ? (uint32_t) bc : hl_;
    if (zurueck_btn_ != nullptr && fokus_ != FOKUS_ZURUECK) {
      lv_obj_set_style_bg_color(zurueck_btn_, lv_color_hex(zurueck_bg_), 0);
      lv_obj_set_style_border_color(zurueck_btn_, lv_color_hex(zurueck_bg_), 0);
    }
  }
  {
    const int32_t bfc = farbe_von(s.j["bfc"] | "");   // Rahmen des markierten Zurueck/X (Konfigurator)
    zurueck_fc_ = bfc >= 0 ? (uint32_t) bfc : 0xF1F3F5;
  }

  const bool flow = s.j["flow"] | true;
  flow_ = flow;
  int W = lv_obj_get_content_width(root_);
  if (W <= 0) W = 240;
  const float zelle = (float) (W - 2 * RAND + LUECKE) / SPALTEN;
  zelle_ = zelle;

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
  items_.reserve(arr.size());   // keine Umlagerung: Regler merken sich ihren Index
  weg_e_.clear();
  wslots_frei_ = 0;
  std::vector<Item> alle;
  alle.reserve(arr.size());
  // Einstellungen: erst die eingebauten Seiten auffrischen (aktuelle Werte und Hinweistexte)
  // Nur die Gruppen dieser Seite (vorher alle neun Skripte bei jeder Einstellungsseite)
  uint32_t maske = 0;
  if (einstellung)
    for (JsonObjectConst j : arr)
      if (strcmp(j["t"] | "", "setting") == 0) {
        const uint8_t g = einstellung(j["src"] | "").gruppe;
        if (g < 31) maske |= 1u << g;
      }
  if (maske && einst_auffrischen) einst_auffrischen(maske);
  for (JsonObjectConst j : arr) {
    Item it;
    it.j = j;
    it.typ = typ_von(j["t"] | "");
    if (it.typ == T_WIDGET && widget_weg_(j)) {   // Medien-Widget ohne Wiedergabe: keine Zeilen
      const char *we = j["entity"] | "";
      if (*we) weg_e_.push_back(PStr(we));
      continue;
    }
    if (it.typ == T_PLAYER) {   // Medien-Element: welches Teil
      const char *t = j["t"] | "";
      if (!strcmp(t, "mtitel")) it.teil = P_INFO;
      else if (!strcmp(t, "vpreset")) { it.teil = P_PRESET; it.preset = (uint8_t) std::max(0, std::min(100, (int) (j["wert"] | 0))); }
      else {
        static const struct { const char *fn; PlayerTeil t; } FN[] = {
            {"play_pause", P_PLAY}, {"play", P_PLAYNUR}, {"pause", P_PAUSE}, {"stop", P_STOP}, {"prev", P_PREV},
            {"next", P_NEXT}, {"mute", P_MUTE}, {"vol_up", P_VOLUP}, {"vol_down", P_VOLDN}, {"shuffle", P_SHUFFLE},
            {"repeat", P_REPEAT}, {"power", P_POWER}};
        const char *fn = j["fn"] | "play_pause";
        it.teil = P_PLAY;
        for (auto &f : FN) if (!strcmp(f.fn, fn)) { it.teil = f.t; break; }
      }
    }
    it.w = std::max(1, std::min(6, (int) (j["w"] | 6)));
    if (it.typ == T_EINST && einstellung) {
      it.einst = einstellung(j["src"] | "");
      // Punkte-Modus ("...") schreibt die Punkte in den Text des Originals - das Menue kopierte dann den
      // gekuerzten Text (Bluetooth: nach dem Antippen kurz ganz, dann wieder "..."). Originale nur abschneiden.
      if (it.einst.wert != nullptr && lv_label_get_long_mode(it.einst.wert) == LV_LABEL_LONG_DOT)
        lv_label_set_long_mode(it.einst.wert, LV_LABEL_LONG_CLIP);
      it.info = (it.einst.typ == 'i' && *(j["label"] | "")) || it.einst.typ == 'g';
    }
    // h = 0: Hoehe nach dem Text (Hinweise/Erklaerungen), sonst mindestens h (1..8) - lange Texte bekommen mehr
    bool st = false;
    it.h = zeilen_(it, zelle, &st);
    it.gestapelt = st;
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
    alle.push_back(it);
  }
  // Fokus: gemerkt oder erstes bedienbares Element (Index in der ganzen Liste)
  auto f = fokus_mem_.find(PStr(stapel_.back().c_str()));
  int start = (f != fokus_mem_.end() && f->second < (int) alle.size()) ? f->second : -1;
  // Markierung beim Oeffnen (Konfigurator, 2026-10-07): first | none | back | i:<Index>. Standard: Popup keine
  // Markierung (erst der erste Druck aufs Steuerkreuz markiert), sonst der erste bedienbare Eintrag.
  bool ohne = nur_anzeige_, zum_x = false;   // Startseite: keine Markierung (Tasten steuern das Geraet)
  if (start < 0 && !ohne) {
    const char *fm = s.j["focus"] | "";
    if (!*fm) fm = popup_ ? "none" : "first";
    if (!strcmp(fm, "none")) ohne = true;
    else if (!strcmp(fm, "back")) zum_x = true;
    else if (!strncmp(fm, "i:", 2)) {
      const int n = atoi(fm + 2);
      if (n >= 0 && n < (int) alle.size() && alle[n].fokusierbar()) start = n;
    }
  }
  if (start < 0 && !ohne && !zum_x)
    for (size_t i = 0; i < alle.size(); i++)
      if (alle[i].fokusierbar()) { start = (int) i; break; }
  // Sofort nur, was man sieht (2026-10-06): Energie & Wecken (61 Eintraege) brauchte ~1,1 s am Stueck -
  // LVGL vermisst jedes Label einzeln. Der Rest entsteht in loop() in Haeppchen von hoechstens 15 ms.
  const int sichtbar = lv_obj_get_height(root_) / ZEILE + 2;
  size_t i = 0;
  for (; i < alle.size(); i++) {
    if (!alles && alle[i].y >= sichtbar && (int) i > start) break;
    items_.push_back(alle[i]);
    bau_eins_(items_.size() - 1);
  }
  wartend_.assign(alle.begin() + i, alle.end());
  wartend_pos_ = 0;
  // Volle Scrollhoehe sofort (2026-10-11): solange der Rest noch in Haeppchen entstand, endete die Seite beim
  // Wischen am bisher Gebauten - schneller Schwung auf "Energie & Wecken" stoppte abrupt. Unsichtbarer Punkt am Ende.
  if (!wartend_.empty()) {
    int unten = 0;
    for (auto &it : alle) unten = std::max(unten, it.y + it.h);
    lv_obj_t *ende = lv_obj_create(root_);
    lv_obj_remove_style_all(ende);
    lv_obj_set_size(ende, 1, 1);
    lv_obj_set_pos(ende, 0, RAND_O + unten * ZEILE - 1);
    lv_obj_clear_flag(ende, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  }
  lv_obj_scroll_to_y(root_, 0, LV_ANIM_OFF);
  if (ohne) fokus_setzen_(-1);
  else fokus_setzen_(start >= 0 && start < (int) items_.size() ? start : FOKUS_ZURUECK);
  // Gemerkte Stelle sofort zeigen statt von oben hinzuscrollen (2026-10-14), auch fuer die Einblend-Welle
  if (!ohne && start >= 0 && start < (int) items_.size()) {
    lv_obj_update_layout(root_);
    lv_obj_scroll_to_view(items_[start].obj, LV_ANIM_OFF);
  }
  abf_ab_ = millis();   // Zustaende sofort holen
  rest_ms_ = 0;
}

void Menue::bau_eins_(size_t i) {
  Item &it = items_[i];
  karte_(it);   // setzt Lage und Groesse gleich nach dem Anlegen (platz_), vor den Kindern
  gestalten_(it);
  extras_(it);
  rueckrufe_leeren_();
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
  rueckrufe_leeren_();
  if (millis() < einbl_bis_) einbl_item_(items_[i]);   // Menue oeffnet gerade: mit einblenden
}

// Rest der Seite: je Runde hoechstens ~15 ms, damit Scrollen/Tasten fluessig bleiben
void Menue::bau_weiter_() {
  if (wartend_pos_ >= wartend_.size()) return;
  // Waehrend der Finger liegt oder die Seite noch schwingt nur kleine Haeppchen (2026-10-11): je Runde 15 ms
  // Aufbau liessen das Wischen auf grossen Seiten (Energie & Wecken) zaeh wirken.
  int64_t grenze_us = 15000;
  for (lv_indev_t *in = lv_indev_get_next(nullptr); in != nullptr; in = lv_indev_get_next(in))
    if (lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER &&
        (lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED || lv_indev_get_scroll_obj(in) != nullptr))
      grenze_us = 4000;
  const int64_t t0 = esp_timer_get_time();
  do {
    items_.push_back(wartend_[wartend_pos_++]);
    bau_eins_(items_.size() - 1);
  } while (wartend_pos_ < wartend_.size() && esp_timer_get_time() - t0 < grenze_us);
  rest_ms_ += (uint32_t) ((esp_timer_get_time() - t0) / 1000);
  if (wartend_pos_ >= wartend_.size()) {
    wartend_.clear();
    wartend_pos_ = 0;
    ESP_LOGD(TAG, "Aufbau Rest fertig: %u Eintraege, %u ms verteilt", (unsigned) items_.size(), (unsigned) rest_ms_);
  }
}

// "#22c55e" -> 0x22C55E, -1 = nicht gesetzt
static int32_t farbe_von(const char *s) {
  if (s == nullptr || *s != '#' || strlen(s) != 7) return -1;
  char *ende = nullptr;
  const long v = strtol(s + 1, &ende, 16);
  return (ende && *ende == 0) ? (int32_t) v : -1;
}

// Gestaltung aus dem Konfigurator (2026-10-05): bg/fg = Farbe "#rrggbb", align = l|c|r, size = s|m|l
void Menue::gestalten_(Item &it) {
  if (it.obj == nullptr || it.typ == T_KAMERA) return;
  const int32_t bg = farbe_von(it.j["bg"] | ""), fg = farbe_von(it.j["fg"] | "");
  const char *al = it.j["align"] | "", *gr = it.j["size"] | "";
  if (bg >= 0) {
    it.bg = (uint32_t) bg;
    lv_obj_set_style_bg_opa(it.obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(it.obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(it.obj, lv_color_lighten(lv_color_hex(bg), 60), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(it.obj, lv_color_hex(bg), 0);
    if (it.typ == T_TEXT) { lv_obj_set_style_radius(it.obj, 8, 0); lv_obj_set_style_pad_hor(it.obj, 6, 0); }
  }
  // Symbol: Farbe "ic", Ausrichtung "ia" (l/c/r; 2026-10-18). In Zeilen steht es links vor dem Namen - rechts
  // heisst dort: ans Ende der Zeile, der Name rueckt nach links.
  if (it.ico != nullptr) {
    const int32_t ic = farbe_von(it.j["ic"] | "");
    if (ic >= 0) lv_obj_set_style_text_color(it.ico, lv_color_hex(ic), 0);
    const char ia = *(it.j["ia"] | "");
    if (ia == 'c' || ia == 'r' || ia == 'l') {
      const bool kachel = lv_obj_get_style_align(it.ico, LV_PART_MAIN) == LV_ALIGN_TOP_MID;
      if (kachel) {
        lv_obj_align(it.ico, ia == 'l' ? LV_ALIGN_TOP_LEFT : ia == 'r' ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_MID, 0, 2);
      } else if (ia == 'r' && lv_obj_get_style_layout(it.obj, LV_PART_MAIN) == LV_LAYOUT_FLEX) {
        lv_obj_move_to_index(it.ico, -1);
      } else if (ia == 'r' && it.name != nullptr) {
        const int pfeil = (it.typ == T_PAGE || it.typ == T_SPECIAL) ? 14 : 0;
        lv_obj_align(it.ico, LV_ALIGN_RIGHT_MID, -pfeil, 0);
        lv_obj_set_x(it.name, std::max<int32_t>(0, lv_obj_get_x_aligned(it.name) - 26));
      } else if (ia == 'c' && it.name == nullptr) {
        lv_obj_center(it.ico);
      }
    }
  }
  if (it.name == nullptr) return;
  if (*gr) {
    const lv_font_t *f = !strcmp(gr, "xs") && f_xs_ ? f_xs_ : !strcmp(gr, "xl") && f_xl_ ? f_xl_
                       : *gr == 's' ? f_klein_ : *gr == 'l' && f_gross_ ? f_gross_ : f_text_;
    if (f) lv_obj_set_style_text_font(it.name, f, 0);
  }
  if (fg >= 0 && !it.name_wert) lv_obj_set_style_text_color(it.name, lv_color_hex(fg), 0);
  if (*al) {
    const lv_text_align_t ta = *al == 'c' ? LV_TEXT_ALIGN_CENTER : *al == 'r' ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;
    lv_obj_set_style_text_align(it.name, ta, 0);
    // Listenzeile ohne Wert: Text ueber die ganze Breite (Kacheln sind es schon), damit "mittig" mittig ist
    if (it.wert == nullptr && it.typ != T_TEXT && it.w > 2 && it.h < 2) {
      const int32_t x = lv_obj_get_x_aligned(it.name);   // > 0: Symbol links davor
      const int32_t breite = (int32_t) (it.w * ((240.0f - 2 * RAND + LUECKE) / SPALTEN)) - LUECKE - 2 * 10 - 4;
      lv_obj_set_width(it.name, breite - x);
      lv_obj_align(it.name, LV_ALIGN_LEFT_MID, x, 0);
    }
  }
}

// Schrift und Farbe VOR dem Text: jedes Setzen danach misst den Text neu (bei Fliesstext teuer)
static lv_obj_t *text_(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t farbe) {
  lv_obj_t *l = lv_label_create(p);
  if (f) lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_label_set_text(l, t);
  return l;
}

// Lage/Groesse sofort nach dem Anlegen: Kinder mit Prozentbreite und Umbruch werden so nur einmal vermessen
// (vorher erst gegen Breite 0, nach dem spaeteren set_size nochmal)
void Menue::platz_(const Item &it, lv_obj_t *o) const {
  lv_obj_set_pos(o, RAND + (int) (it.x * zelle_), RAND_O + it.y * ZEILE);
  lv_obj_set_size(o, (int) (it.w * zelle_) - LUECKE, it.h * ZEILE - LUECKE);
}

// Gemeinsamer Kartenstil statt acht lokaler Stil-Eigenschaften je Karte (2026-10-06, Aufbauzeit)
static lv_style_t *karten_stil_() {
  static lv_style_t s, *p = nullptr;
  if (p == nullptr) {
    lv_style_init(&s);
    lv_style_set_bg_opa(&s, LV_OPA_COVER);
    lv_style_set_bg_color(&s, lv_color_hex(C_KARTE));
    lv_style_set_radius(&s, 10);
    lv_style_set_border_width(&s, 2);
    lv_style_set_border_color(&s, lv_color_hex(C_KARTE));
    lv_style_set_pad_ver(&s, 4);
    p = &s;
  }
  return p;
}
static lv_style_t *karten_stil_gedrueckt_() {
  static lv_style_t s, *p = nullptr;
  if (p == nullptr) { lv_style_init(&s); lv_style_set_bg_color(&s, lv_color_hex(C_FOKUS)); p = &s; }
  return p;
}

// Regler unten in einer zweizeiligen Karte (Einstellung, Licht, Zahl)
static lv_obj_t *regler_(lv_obj_t *k, int32_t min, int32_t max) {
  lv_obj_t *sl = lv_slider_create(k);
  lv_slider_set_range(sl, min, max);
    lv_obj_set_width(sl, LV_PCT(90));
    lv_obj_set_height(sl, 8);
    lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x4B5563), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(g_hl), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(C_TEXT), LV_PART_KNOB);
  return sl;
}

void Menue::karte_(Item &it) {
  if (it.typ == T_PLAYER) { player_karte_(it); return; }
  if (it.typ == T_WIDGET) { widget_karte_(it); return; }
  const char *label = it.j["label"] | "";
  if (it.typ == T_TEXT) {
    lv_obj_t *o = lv_obj_create(root_);
    lv_obj_remove_style_all(o);
    platz_(it, o);
    lv_obj_clear_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_t *l = text_(o, label, f_klein_, C_GRAU);
    it.name = l;
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
    platz_(it, o);
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
    platz_(it, k);
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
  lv_obj_add_style(k, karten_stil_(), 0);
  lv_obj_add_style(k, karten_stil_gedrueckt_(), LV_STATE_PRESSED);
  lv_obj_set_style_pad_hor(k, it.w <= 2 ? 3 : 10, 0);
  platz_(it, k);
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(k, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  it.obj = k;

  const bool klein = it.w <= 2;
  const bool hoch = it.h >= 2;
  const char *ic = icon_(it.j["icon"] | "");
  const lv_font_t *f = klein ? f_klein_ : f_text_;
  const bool mit_wert = it.typ == T_TOGGLE || it.typ == T_LIGHT || it.typ == T_COVER || it.typ == T_SENSOR || it.typ == T_ZAHL ||
                        it.typ == T_AUSWAHL || (it.typ == T_EINST && it.einst.wert != nullptr);
  if (it.typ == T_EINST && it.einst.typ == 'g') {
    // Linien-Grafik (Akku-Verlauf 24 h) direkt im Menue statt auf der eingebauten Seite (2026-10-07)
    const int cw = (int) (it.w * zelle_) - LUECKE - 20, gh = it.h * ZEILE - 28;
    lv_obj_t *mitte = lv_obj_create(k);
    lv_obj_remove_style_all(mitte);
    lv_obj_set_style_bg_opa(mitte, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mitte, lv_color_hex(0x2E3643), 0);
    lv_obj_set_size(mitte, cw, 1);
    lv_obj_set_pos(mitte, 0, gh / 2);
    lv_obj_t *li = lv_line_create(k);
    lv_obj_set_pos(li, 0, 0);
    lv_obj_set_size(li, cw, gh);
    lv_obj_set_style_line_width(li, 2, 0);
    lv_obj_set_style_line_color(li, lv_color_hex(hl_), 0);
    lv_obj_set_style_line_rounded(li, true, 0);
    it.regler = li;
    lv_obj_align(text_(k, "vor 24 h", f_klein_, C_GRAU), LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(text_(k, "jetzt", f_klein_, C_GRAU), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    it.name = text_(k, "noch keine Daten", f_klein_, C_GRAU);
    lv_obj_align(it.name, LV_ALIGN_TOP_MID, 0, gh / 2 - 8);
    return;
  }
  if (it.typ == T_EINST && it.einst.typ != 'b' && it.einst.typ != 's') {
    // reine Anzeige: ohne Kartenhintergrund, Name links, Wert rechts (mit Fokus hinterlegt)
    lv_obj_set_style_bg_opa(k, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(k, 0, 0);
    it.durchsichtig = true;
  }
  // Regler auch einzeilig (2026-10-29, Nutzer: "geht nur, wenn der zweizeilig ist"): ab 4 Spalten Name, Regler, Wert
  const bool regler_zeile = hoch || it.w >= 4;
  if (regler_zeile && it.typ == T_EINST && it.einst.typ == 's' && it.einst.obj != nullptr) {
    // Kopfzeile Name / Wert, darunter ein Regler mit dem Bereich des Originals
    lv_obj_t *l = text_(k, label, f, C_TEXT);
    it.name = l;
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(62));
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 4);
    it.wert = text_(k, "", f_klein_, C_GRAU);
    lv_obj_align(it.wert, LV_ALIGN_TOP_RIGHT, 0, 6);
    lv_obj_t *sl = regler_(k, lv_slider_get_min_value(it.einst.obj), lv_slider_get_max_value(it.einst.obj));
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
    if (!hoch) regler_einzeilig_(it, k, sl);
    return;
  }

  if ((hoch && (it.typ == T_LIGHT || it.typ == T_COVER || it.typ == T_ZAHL)) || (regler_zeile && (it.typ == T_LIGHT || it.typ == T_ZAHL))) {
    // Kopfzeile: Name links, Wert rechts; darunter Regler bzw. Rollo-Knoepfe
    lv_obj_t *l = text_(k, label, f, C_TEXT);
    it.name = l;
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(65));
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 4);
    it.wert = text_(k, "", f_klein_, C_GRAU);
    lv_obj_align(it.wert, LV_ALIGN_TOP_RIGHT, 0, 6);
    if (it.typ == T_LIGHT || it.typ == T_ZAHL) {
      lv_obj_t *sl = regler_(k, 0, 100);
      lv_obj_set_user_data(sl, (void *) (intptr_t) (&it - items_.data()));
      lv_obj_add_event_cb(sl, Menue::regler_cb_, LV_EVENT_RELEASED, this);
      it.regler = sl;
      if (!hoch) regler_einzeilig_(it, k, sl);
    } else {
      static const char *const T[3] = {"Zu", "Stop", "Auf"};
      for (int d = 0; d < 3; d++) {
        lv_obj_t *b = lv_obj_create(k);
        lv_obj_remove_style_all(b);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x232A35), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(hl_), LV_STATE_PRESSED);
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

  const char lay = *(it.j["lay"] | "");   // Anordnung (2026-10-26): 1 Zeile, 2 zweizeilig, k Kachel, g gross, i Symbol
  if (lay == '2' || lay == 'g' || lay == 'i') { layout_eigen_(it, k, ic, label, mit_wert, lay); return; }
  if (it.gestapelt && lay != '1') {
    // Name oben (bricht um), Wert darunter, als Block senkrecht mittig (wie die Bluetooth-Zeilen der eingebauten Seite)
    const int pfeil = (it.typ == T_PAGE || it.typ == T_SPECIAL) ? 14 : 0;
    int x = 0;
    if (ic && f_icon_) {
      it.ico = text_(k, ic, f_icon_, C_TEXT);
      lv_obj_align(it.ico, LV_ALIGN_LEFT_MID, 0, 0);
      x = 26;
    }
    const int frei = (int) (it.w * zelle_) - LUECKE - 20 - x - pfeil;
    const char *wert = mit_wert ? wert_text_(it) : "";
    lv_point_t n{0, 0}, w{0, 0};
    if (*label) lv_text_get_size(&n, label, f_text_, 0, 0, frei, LV_TEXT_FLAG_NONE);
    if (mit_wert) lv_text_get_size(&w, *wert ? wert : "-", f_klein_, 0, 0, frei, LV_TEXT_FLAG_NONE);
    const int block = n.y + (mit_wert ? (*label ? 2 : 0) + w.y : 0);
    const int y0 = std::max(0, (it.h * ZEILE - LUECKE - 8 - block) / 2);
    lv_obj_t *l = text_(k, label, f_text_, C_TEXT);
    it.name = l;
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, frei);
    lv_obj_set_pos(l, x, y0);
    if (mit_wert) {
      it.wert = text_(k, "", f_klein_, C_GRAU);
      lv_label_set_long_mode(it.wert, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(it.wert, frei);
      lv_obj_set_pos(it.wert, x, y0 + n.y + (*label ? 2 : 0));
    }
    if (pfeil) lv_obj_align(text_(k, ">", f_text_, C_GRAU), LV_ALIGN_RIGHT_MID, 0, 0);
    return;
  }

  if ((hoch || klein || lay == 'k') && lay != '1') {
    // Kachel: Icon oben, Name darunter, Wert ganz unten (zentriert)
    lv_obj_t *ico = nullptr;
    if (ic && f_icon_) { ico = it.ico = text_(k, ic, f_icon_, C_TEXT); }
    lv_obj_t *l = text_(k, label, f, C_TEXT);
    it.name = l;
    lv_label_set_long_mode(l, hoch ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (!hoch && !strchr(label, ' ') && !strchr(label, '-')) {
      // Ein Wort, das nicht in die kleine Kachel passt ("Wohnungst."): enger setzen statt mitten im Wort
      // umbrechen; passt es dann noch nicht, einzeilig mit "..." (2026-10-11)
      lv_point_t g;
      const int innen = (int) (it.w * zelle_) - LUECKE - 6;
      lv_text_get_size(&g, label, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      if (g.x > innen) {
        const int n = (int) strlen(label);
        lv_obj_set_style_text_letter_space(l, std::max(-2, (int) (-(g.x - innen + n - 1) / std::max(1, n))), 0);
        lv_obj_set_height(l, lv_font_get_line_height(f));
      }
    }
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
      if (ico) { lv_obj_delete(ico); it.ico = nullptr; }
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
    lv_obj_t *ico = it.ico = text_(k, ic, f_icon_, C_TEXT);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);
    x = 26;
  }
  // schmaler Knopf ohne Wert, dessen Name nicht passt: kleinere Schrift (s. zeilen_)
  if (!mit_wert && it.w < 4 && f == f_text_) {
    lv_point_t g;
    lv_text_get_size(&g, label, f_text_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (g.x > (int) (it.w * zelle_) - LUECKE - 20 - x - ((it.typ == T_PAGE || it.typ == T_SPECIAL) ? 14 : 0)) f = f_klein_;
  }
  if (mit_wert && !flow_ && it.w <= 3 && !hoch) {
    // Schmale Karte mit Wert auf fest angeordneter Seite (2026-10-11, "Position | Einfahrt" im Kamera-Popup):
    // nebeneinander passt beides nicht, wachsen darf die Karte nicht - Name und Wert klein untereinander.
    lv_obj_set_style_pad_ver(k, 1, 0);
    lv_obj_t *l = text_(k, label, f_klein_, C_TEXT);
    it.name = l;
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_size(l, (int) (it.w * zelle_) - LUECKE - 20 - x, lv_font_get_line_height(f_klein_));
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, -3);   // etwas in den Rand: zwei Zeilen in 36 px
    it.wert = text_(k, "", f_klein_, C_GRAU);
    lv_label_set_long_mode(it.wert, LV_LABEL_LONG_DOT);
    lv_obj_set_size(it.wert, (int) (it.w * zelle_) - LUECKE - 20 - x, lv_font_get_line_height(f_klein_));
    lv_obj_align(it.wert, LV_ALIGN_BOTTOM_LEFT, x, 3);
    return;
  }
  if (mit_wert && f == f_text_) {
    // Name + Wert in einer Zeile: passt der Name neben einen kurzen Wert nicht, kleinere Schrift (2026-10-11)
    lv_point_t g;
    lv_text_get_size(&g, label, f_text_, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (g.x + 40 > (int) (it.w * zelle_) - LUECKE - 20 - x) f = f_klein_;
  }
  lv_obj_t *l = text_(k, label, f, C_TEXT);
  it.name = l;
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  if (mit_wert) {
    // Name nimmt den ganzen Platz neben dem Wert (vorher fest 60 %: "Größe auf dem TV" und "Position" brachen
    // mitten im Wort auf eine zweite Zeile um, 2026-10-11). Einzeilig: zu lange Namen enden mit "...".
    lv_obj_set_layout(k, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(k, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(k, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(k, 8, 0);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_width(l, 1);
    if (!hoch) lv_obj_set_height(l, lv_font_get_line_height(f));
    it.wert = text_(k, "", f_klein_, C_GRAU);
  } else {
    // ohne Wert die ganze Breite (vorher 80 % - "Chromecast" wurde in einem halben Knopf gekuerzt), Pfeil > abziehen
    lv_obj_set_width(l, (int) (it.w * zelle_) - LUECKE - 20 - x - ((it.typ == T_PAGE || it.typ == T_SPECIAL) ? 14 : 0));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
  }
  if (!mit_wert && (it.typ == T_PAGE || it.typ == T_SPECIAL)) {
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

void Menue::grafik_zeigen_(Item &it) {
  if (it.einst.obj == nullptr || it.regler == nullptr) return;
  const uint32_t n = lv_line_get_point_count(it.einst.obj);
  const lv_point_precise_t *p = lv_line_get_points(it.einst.obj);
  // Original: 216 x 60 (page_akku) -> auf die Karte skalieren
  const float sx = ((int) (it.w * zelle_) - LUECKE - 20) / 216.0f, sy = (it.h * ZEILE - 28) / 60.0f;
  bool leer = n < 2 || p == nullptr;
  PVec<lv_point_precise_t> neu;
  if (!leer) {
    neu.resize(n);
    leer = true;
    for (uint32_t i = 0; i < n; i++) {
      neu[i].x = (lv_value_precise_t) (p[i].x * sx);
      neu[i].y = (lv_value_precise_t) (p[i].y * sy);
      if (p[i].x != p[0].x || p[i].y != p[0].y) leer = false;
    }
  }
  if (it.name != nullptr) {
    if (leer) lv_obj_remove_flag(it.name, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(it.name, LV_OBJ_FLAG_HIDDEN);
  }
  if (leer) neu.clear();
  if (neu.size() == grafik_pkt_.size() &&
      (neu.empty() || memcmp(neu.data(), grafik_pkt_.data(), neu.size() * sizeof(lv_point_precise_t)) == 0))
    return;
  grafik_pkt_ = neu;
  lv_line_set_points(it.regler, grafik_pkt_.data(), grafik_pkt_.size());
}

void Menue::einst_zeigen_(Item &it) {
  if (it.einst.typ == 'g') { grafik_zeigen_(it); return; }
  if (it.einst.wert != nullptr && it.wert != nullptr) {
    const lv_color_t farbe = lv_obj_get_style_text_color(it.einst.wert, LV_PART_MAIN);
    if (!it.name_wert) {
      const char *neu = lv_label_get_text(it.einst.wert);
      if (strcmp(lv_label_get_text(it.wert), neu) != 0) {
        lv_label_set_text(it.wert, neu);
        hoehen_pruefen_();   // laengerer Wert braucht evtl. eine Zeile mehr
      }
    }
    // Nur bei Aenderung: laeuft alle 700 ms fuer jeden Eintrag, jedes Setzen erzwingt eine Stil-Neuberechnung
    if (!lv_color_eq(lv_obj_get_style_text_color(it.wert, LV_PART_MAIN), farbe)) lv_obj_set_style_text_color(it.wert, farbe, 0);
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
    bool st = false;
    const int z = zeilen_(it, zelle, &st);
    // Feste Hoehe: nur wachsen (sonst koennte ein Wert an der Grenze die Seite staendig neu legen)
    const bool anders = (int) (it.j["h"] | 1) <= 0 ? z != it.h : (z > it.h || (st && !it.gestapelt));
    if (anders) {
      ESP_LOGD(TAG, "Texthoehe geaendert -> Seite neu");
      const int f = fokus_;
      const int32_t sy = lv_obj_get_scroll_y(root_);
      bauen_(true);
      if (f >= 0 && f < (int) items_.size()) fokus_setzen_(f);
      lv_obj_scroll_to_y(root_, sy, LV_ANIM_OFF);
      return;
    }
  }
}

void Menue::zustand_(Item &it) {
  if (it.typ == T_EINST) { einst_zeigen_(it); return; }
  if (it.typ == T_WIDGET) { widget_zustand_(it); return; }
  if (it.typ == T_PLAYER) { player_zustand_(it); std::string d; extras_zustand_(it, d); return; }
  if (it.wert == nullptr || it.typ == T_KAMERA) {
    if (it.typ != T_KAMERA) { std::string d; extras_zustand_(it, d); }
    return;
  }
  const char *e = it.j["entity"] | "";
  auto f = zustand_cache_.find(PStr(e));
  if (!*e || f == zustand_cache_.end() || f->second.state.empty()) {
    if (!it.name_wert) lv_label_set_text(it.wert, "–");
    std::string d;
    extras_zustand_(it, d);
    return;
  }
  const EntZustand &z = f->second;
  const bool an = z.state == "on" || z.state == "open" || z.state == "playing" ||
                  (it.typ == T_ZAHL && strncmp(e, "media_player.", 13) == 0 && z.state != "off" && z.state != "unavailable");
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
    case T_SENSOR: {
      // Englische Grundzustaende von HA auf Deutsch (2026-10-11: Kamera-Popup zeigte "on"/"off")
      static const char *const DE[][2] = {{"on", "an"}, {"off", "aus"}, {"open", "offen"}, {"closed", "zu"},
                                          {"unavailable", "nicht da"}, {"unknown", "–"}, {"home", "da"},
                                          {"not_home", "weg"}, {"playing", "spielt"}, {"paused", "pausiert"},
                                          {"idle", "bereit"}, {"standby", "Standby"}, {"active", "läuft"}};
      const char *s = z.state.c_str();
      for (auto &d : DE) if (z.state == d[0]) { s = d[1]; break; }
      snprintf(b, sizeof(b), "%s%s%s", s, z.unit.empty() ? "" : " ", z.unit.c_str());
      break;
    }
    case T_ZAHL:
      if (strncmp(e, "media_player.", 13) == 0) {
        auto m = z.attr.find(PStr("is_volume_muted"));
        const bool stumm = m != z.attr.end() && m->second == "true";
        if (z.state == "off") snprintf(b, sizeof(b), "aus");
        else if (stumm) snprintf(b, sizeof(b), "stumm · %d %%", std::max(0, z.pos));
        else if (z.pos >= 0) snprintf(b, sizeof(b), "%d %%", z.pos);
        else snprintf(b, sizeof(b), "%s", z.state.c_str());
        if (it.regler && !lv_obj_has_state(it.regler, LV_STATE_PRESSED)) {
          lv_slider_set_range(it.regler, 0, 100);
          lv_slider_set_value(it.regler, std::max(0, z.pos), LV_ANIM_OFF);
        }
      } else {
        snprintf(b, sizeof(b), "%g%s%s", atof(z.state.c_str()), z.unit.empty() ? "" : " ", z.unit.c_str());
        if (it.regler && !lv_obj_has_state(it.regler, LV_STATE_PRESSED)) {
          lv_slider_set_range(it.regler, (int32_t) z.mn, (int32_t) z.mx);
          lv_slider_set_value(it.regler, (int32_t) std::lround(atof(z.state.c_str())), LV_ANIM_OFF);
        }
      }
      break;
    default:
      snprintf(b, sizeof(b), "%s", z.state == "on" ? "an" : z.state == "off" ? "aus" : z.state.c_str());
  }
  std::string w = b;
  extras_zustand_(it, w);   // Umbenennen, Nachkommastellen, zweite Zeile, Stil je Zustand, Bedingung
  if (it.name_wert) {
    if (it.j["zst"].isNull()) lv_obj_set_style_text_color(it.wert, lv_color_hex(an ? C_AN : C_TEXT), 0);
    return;
  }
  if (strcmp(lv_label_get_text(it.wert), w.c_str()) != 0) {
    lv_label_set_text(it.wert, w.c_str());
    hoehen_pruefen_();
  }
  // Auswahl (Eingang, Effekt ...): Wert weiss wie die Auswahl-Werte der Einstellungen
  lv_obj_set_style_text_color(it.wert, lv_color_hex(it.typ == T_AUSWAHL ? C_TEXT : an && it.typ != T_SENSOR ? C_AN : C_GRAU), 0);
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
  if (self->lang_war_) { self->lang_war_ = false; return; }   // war ein langer Druck ("lang")
  // Licht-Widget: Tippen auf die Karte markiert nur - geschaltet wird ueber den Knopf (OK-Taste schaltet weiter)
  if (self->items_[i].typ == T_WIDGET && self->items_[i].wart == W_LICHT) return;
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
  // Seiten-Option "vol" (2026-10-26): Lauter/Leiser/Stumm bedienen diesen Player (z. B. den AV-Receiver), Schritt "vst" %.
  // Halten wiederholt (erst nach 450 ms, dann alle 250 ms - jeder Schritt ist ein Aufruf an HA).
  if (k == 33 || k == 43 || k == 44) {
    JsonObjectConst sj = seite_j_();
    const std::string e = sj["vol"] | "";
    if (!e.empty()) {
      cancel_timeout("vol_wdh");
      cancel_interval("vol_wdh");
      if (!gedrueckt) return true;
      if (k == 44) { player_stumm_(e); return true; }
      const int st = std::max(1, (int) (sj["vst"] | 5));
      const int d = k == 33 ? st : -st;
      player_lautst_(e, d);
      set_timeout("vol_wdh", 450, [this, e, d]() { set_interval("vol_wdh", 250, [this, e, d]() { player_lautst_(e, d); }); });
      return true;
    }
  }
  // OK lang (2026-10-26): Eintrag mit "lang" - kurz = normale Aktion beim Loslassen, gehalten (600 ms) = "lang"
  if (k == 34 && !tmodus_ && fokus_ >= 0 && fokus_ < (int) items_.size() && !items_[fokus_].j["lang"].isNull() &&
      !items_[fokus_].gesperrt) {
    if (gedrueckt) {
      ok_lang_i_ = fokus_;
      ok_lang_fertig_ = false;
      set_timeout("ok_lang", 600, [this]() {
        if (ok_lang_i_ < 0 || ok_lang_i_ >= (int) items_.size()) return;
        ok_lang_fertig_ = true;
        tk_->schritte(items_[ok_lang_i_].j["lang"].as<JsonArrayConst>());
      });
    } else {
      cancel_timeout("ok_lang");
      if (!ok_lang_fertig_ && ok_lang_i_ == fokus_) ausloesen_(items_[fokus_]);
      ok_lang_i_ = -1;
    }
    return true;
  }
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
  int v = lv_slider_get_value(sl);
  // Grenzen (Konfigurator "vmin"/"vmax", z. B. Lautstaerke hoechstens 60 %)
  const int lo = self->items_[i].j["vmin"] | INT32_MIN, hi = self->items_[i].j["vmax"] | INT32_MAX;
  if (v < lo || v > hi) { v = std::max(lo, std::min(hi, v)); lv_slider_set_value(sl, v, LV_ANIM_OFF); }
  const char *mo = self->items_[i].j["modus"] | "";
  if (*mo) self->tk_->modus_starten(mo);
  if (self->items_[i].typ == T_ZAHL) { self->zahl_setzen_(self->items_[i], (float) v); return; }
  if (v <= 0) self->tk_->ha_aufruf("light.turn_off", {{"entity_id", ent}});
  else self->tk_->ha_aufruf("light.turn_on", {{"entity_id", ent}, {"brightness_pct", std::to_string(v)}});
}

// number/input_number: set the value (clamped to the range); media_player: volume 0..100
void Menue::zahl_setzen_(Item &it, float wert) {
  const std::string e = it.j["entity"] | "";
  if (e.empty()) return;
  char b[24];
  if (e.rfind("media_player.", 0) == 0) { lautst_setzen_(e, (int) std::lround(wert)); return; }
  auto f = zustand_cache_.find(PStr(e.c_str()));
  if (f != zustand_cache_.end()) wert = std::max(f->second.mn, std::min(f->second.mx, wert));
  snprintf(b, sizeof(b), "%g", wert);
  tk_->ha_aufruf(e.rfind("input_number.", 0) == 0 ? "input_number.set_value" : "number.set_value",
                 {{"entity_id", e}, {"value", b}});
  abf_ab_ = millis() + 900;
}

// richtung: 0 = Tippen/OK, -1/+1 = Links/Rechts (Licht dimmen, Rollo zu/auf), 2 = Rollo Stop
void Menue::ausloesen_(Item &it, int richtung) {
  if (it.gesperrt) return;   // Bedingung ("wenn") nicht erfuellt
  if ((it.j["sicher"] | false) && richtung == 0 && !sicher_ok_(it)) return;
  abf_ab_ = millis() + 900;   // kurz danach neuen Zustand holen
  // Folgemodus nach dem Bedienen (cfg.modi), z. B. Ventilator: danach steuert Kanal +/- die Stufe
  const char *mo = it.j["modus"] | "";
  if (*mo) tk_->modus_starten(mo);
  const std::string e = it.j["entity"] | "";
  JsonArrayConst steps = it.j["steps"];
  const bool eigene = !steps.isNull() && steps.size() > 0;
  auto ha = [&](const char *svc) { if (!e.empty()) tk_->ha_aufruf(svc, {{"entity_id", e}}); };
  switch (it.typ) {
    case T_WIDGET: widget_ausloesen_(it, richtung); return;
    case T_PAGE: {
      const char *z = it.j["target"] | "";
      if (seite_index_(z) < 0) { ESP_LOGW(TAG, "Seite '%s' fehlt", z); return; }
      if (nur_anzeige_) { if (seite_oeffnen) seite_oeffnen(z); return; }   // Startseite: ins richtige Menue
      if (fokus_ >= 0) fokus_mem_[PStr(stapel_.back().c_str())] = fokus_;
      stapel_.push_back(z);
      rutsch_ = 1;
      bauen_();
      rutschen_(1);
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
        else tk_->ha_aufruf("light.turn_on", {{"entity_id", e}, {"brightness_pct", std::to_string(b)}});
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
    case T_ZAHL: {
      const bool media = e.rfind("media_player.", 0) == 0;
      if (richtung == -1 || richtung == 1) {
        if (media) {   // eigene Schrittweite in % (st), sonst die des Geraets
          const int st = it.j["st"] | 0;
          if (st > 0) player_lautst_(e, richtung * st);
          else ha(richtung > 0 ? "media_player.volume_up" : "media_player.volume_down");
          return;
        }
        auto f = zustand_cache_.find(PStr(e.c_str()));
        if (f == zustand_cache_.end() || f->second.state.empty()) return;
        const EntZustand &z = f->second;
        // ~30 presses over the whole range, at least one step
        const float sch = z.schritt > 0 ? z.schritt : 1.0f;
        const float d = std::max(sch, std::round((z.mx - z.mn) / 30.0f / sch) * sch);
        zahl_setzen_(it, atof(z.state.c_str()) + richtung * d);
        return;
      }
      if (eigene) tk_->schritte(steps);
      else if (media && strcmp(it.j["ok"] | "", "mute") == 0) player_stumm_(e);
      else if (media) ha("media_player.toggle");
      return;
    }
    case T_PLAYER: {
      if (eigene) { tk_->schritte(steps); return; }
      if (e.empty()) return;
      EntZustand &z = zustand_cache_[PStr(e.c_str())];
      auto attr = [&](const char *a) -> std::string {
        auto g = z.attr.find(PStr(a));
        return g == z.attr.end() ? std::string() : std::string(g->second.c_str());
      };
      switch (it.teil) {
        case P_MUTE: player_stumm_(e); return;
        case P_PREV: ha("media_player.media_previous_track"); return;
        case P_PLAY: ha("media_player.media_play_pause"); return;
        case P_PLAYNUR: ha("media_player.media_play"); return;
        case P_PAUSE: ha("media_player.media_pause"); return;
        case P_STOP: ha("media_player.media_stop"); return;
        case P_NEXT: ha("media_player.media_next_track"); return;
        case P_VOLUP: ha("media_player.volume_up"); return;
        case P_VOLDN: ha("media_player.volume_down"); return;
        case P_POWER: ha("media_player.toggle"); return;
        case P_SHUFFLE: {
          const bool an = attr("shuffle") != "true";
          tk_->ha_aufruf("media_player.shuffle_set", {{"entity_id", e}, {"shuffle", an ? "true" : "false"}});
          z.attr[PStr("shuffle")] = an ? "true" : "false";
          aktualisieren_(e);
          abf_ab_ = millis() + 1500;
          return;
        }
        case P_REPEAT: {   // aus -> alle -> eins -> aus
          const std::string r = attr("repeat");
          const char *neu = r == "all" ? "one" : r == "one" ? "off" : "all";
          tk_->ha_aufruf("media_player.repeat_set", {{"entity_id", e}, {"repeat", neu}});
          z.attr[PStr("repeat")] = neu;
          aktualisieren_(e);
          abf_ab_ = millis() + 1500;
          return;
        }
        case P_PRESET: {
          int ziel = it.preset;
          JsonArrayConst ws = it.j["werte"];
          if (ws.size() >= 2) {   // Preset-Leiste: OK = naechster Wert, Links/Rechts = Nachbar
            int n = (int) ws.size(), cur = -1;
            for (int i = 0; i < n; i++) if (z.pos >= 0 && std::abs(z.pos - ws[i].as<int>()) <= 2) cur = i;
            int neu = richtung == 0 ? (cur + 1) % n : cur < 0 ? (richtung > 0 ? 0 : n - 1) : std::max(0, std::min(n - 1, cur + richtung));
            ziel = ws[neu].as<int>();
          }
          lautst_setzen_(e, ziel);
          if (attr("is_volume_muted") == "true") player_stumm_(e);   // Preset hebt Stumm auf
          return;
        }
        default: return;
      }
    }
    case T_AUSWAHL: {
      if (eigene && richtung == 0) { tk_->schritte(steps); return; }
      if (e.empty()) return;
      const bool inp = e.rfind("input_select.", 0) == 0;
      const char *svc = richtung < 0 ? (inp ? "input_select.select_previous" : "select.select_previous")
                                     : (inp ? "input_select.select_next" : "select.select_next");
      tk_->ha_aufruf(svc, {{"entity_id", e}, {"cycle", "true"}});
      return;
    }
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
  // X/Zurueck ist blau wie das aktive Dock-Segment (st_back, 2026-10-13); Fokus = weisser Rahmen
  if (fokus_ == FOKUS_ZURUECK && zurueck_btn_ != nullptr) {
    lv_obj_set_style_bg_color(zurueck_btn_, lv_color_hex(zurueck_bg_), 0);
    lv_obj_set_style_border_color(zurueck_btn_, lv_color_hex(zurueck_bg_), 0);
    if (zurueck_lbl_) lv_obj_set_style_text_color(zurueck_lbl_, lv_color_hex(C_TEXT), 0);
  } else if (fokus_ >= 0 && fokus_ < (int) items_.size()) {
    lv_obj_t *o = items_[fokus_].obj;
    lv_obj_set_style_bg_color(o, lv_color_hex(items_[fokus_].bg), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(items_[fokus_].rand >= 0 ? (uint32_t) items_[fokus_].rand : items_[fokus_].bg), 0);
    if (items_[fokus_].durchsichtig) {
      lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(o, 0, 0);
    }
  }
  fokus_ = idx;
  if (idx == FOKUS_ZURUECK && zurueck_btn_ != nullptr) {
    // wie die eingebauten Seiten: Knopf hell hinterlegt, Zeichen weiss
    lv_obj_set_style_bg_color(zurueck_btn_, lv_color_hex(zurueck_bg_), 0);
    lv_obj_set_style_border_color(zurueck_btn_, lv_color_hex(zurueck_fc_), 0);   // Seite "bfc", Standard weiss
    if (zurueck_lbl_) lv_obj_set_style_text_color(zurueck_lbl_, lv_color_hex(C_TEXT), 0);
    lv_obj_scroll_to_y(root_, 0, LV_ANIM_ON);
    return;
  }
  if (idx < 0 || idx >= (int) items_.size()) return;
  lv_obj_t *o = items_[idx].obj;
  if (items_[idx].durchsichtig) {
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 2, 0);
  }
  // Farbige Knoepfe behalten ihre Farbe, nur der Rahmen zeigt den Fokus. Standard: blauer Rahmen fuer alle
  // (2026-10-13, vorher farbige Knoepfe weiss); "fc" (Konfigurator "Markierung") gibt die Rahmenfarbe vor.
  if (items_[idx].bg == C_KARTE) lv_obj_set_style_bg_color(o, lv_color_hex(C_FOKUS), 0);
  const int32_t fc = farbe_von(items_[idx].j["fc"] | "");
  lv_obj_set_style_border_color(o, lv_color_hex(fc >= 0 ? (uint32_t) fc : hl_), 0);
  lv_obj_scroll_to_view(o, LV_ANIM_ON);
}

void Menue::fokus(int dx, int dy) {
  // Seite noch nicht fertig aufgebaut (bau_weiter_): vom X nach oben springt der Fokus ans Ende -> Rest sofort;
  // nach unten kurz vor dem Ende des Aufgebauten -> ein Haeppchen mehr
  if (wartend_pos_ < wartend_.size()) {
    const bool rueck = dy < 0 || dx < 0;
    if (fokus_ < 0 && rueck) {
      while (wartend_pos_ < wartend_.size()) bau_weiter_();
    } else if (!rueck && fokus_ >= 0 && (int) items_.size() - fokus_ <= 6) {
      bau_weiter_();
    }
  }
  // Erstes/letztes nach Position, nicht nach Listenreihenfolge (2026-10-12: im Kamera-Popup steht "D-Pad" als letzter
  // Eintrag in Reihe 8 - vom X nach oben landete der Fokus dort statt ganz unten). Oben links bzw. unten links.
  int erstes = -1, letztes = -1;
  for (size_t i = 0; i < items_.size(); i++) {
    const Item &b = items_[i];
    if (!b.fokusierbar()) continue;
    if (erstes < 0 || b.y < items_[erstes].y || (b.y == items_[erstes].y && b.x < items_[erstes].x)) erstes = (int) i;
    if (letztes < 0 || b.y + b.h > items_[letztes].y + items_[letztes].h ||
        (b.y + b.h == items_[letztes].y + items_[letztes].h && b.x < items_[letztes].x)) letztes = (int) i;
  }
  // Reihenfolge wie auf den eingebauten Seiten: Eintraege ..., dann X/Zurueck, dann wieder oben.
  if (fokus_ == FOKUS_ZURUECK || fokus_ < 0) {
    if (erstes < 0) { fokus_setzen_(FOKUS_ZURUECK); return; }
    fokus_setzen_((dy < 0 || dx < 0) ? letztes : erstes);
    return;
  }
  Item &a = items_[fokus_];
  // Links/Rechts auf breitem Licht/Rollo: Wert aendern statt Fokus bewegen
  if (dx != 0 && dy == 0 && a.w >= 4 && (a.typ == T_LIGHT || a.typ == T_COVER || a.typ == T_ZAHL || a.typ == T_AUSWAHL)) { ausloesen_(a, dx); return; }
  if (dx != 0 && dy == 0 && a.teil == P_PRESET && a.j["werte"].size() >= 2) { ausloesen_(a, dx); return; }
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
      // Zuerst die naechste Reihe (Abstand der Kanten), darin was sich waagrecht ueberschneidet (2026-10-11:
      // von "Zelt" nach unten sprang der Fokus ueber die breite Karte "Größe auf dem TV" hinweg auf "Klein")
      haupt = (dy > 0 ? b.y - (a.y + a.h) : a.y - (b.y + b.h)) * 100.0f;
      const bool ueber = b.x < a.x + a.w && b.x + b.w > a.x;
      neben = ueber ? std::abs(bx - ax) * 0.01f : std::abs(bx - ax);
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

// ------------------------------------------------------------------ Medien-Steuerung (2026-10-26)

JsonObjectConst Menue::seite_j_() const {
  if (stapel_.empty()) return JsonObjectConst();
  const int si = seite_index_(stapel_.back());
  return si >= 0 ? seiten_[si].j : JsonObjectConst();
}

void Menue::player_lautst_(const std::string &e, int d) {
  if (d == 0) return;
  const EntZustand &z = zustand_cache_[PStr(e.c_str())];
  if (z.pos < 0) {   // Lautstaerke unbekannt (z. B. Infrarot-Geraet ohne Rueckmeldung): Schritt statt Wert setzen
    tk_->ha_aufruf(d > 0 ? "media_player.volume_up" : "media_player.volume_down", {{"entity_id", e}});
    abf_ab_ = millis() + 900;
    return;
  }
  lautst_setzen_(e, z.pos + d);
}

// Lautstaerke 0..100 setzen und gleich zeigen (Regler, Presets) - auch ohne Regler auf der Seite
void Menue::lautst_setzen_(const std::string &e, int v) {
  v = lautst_grenze_(e, std::max(0, std::min(100, v)));
  char b[12];
  snprintf(b, sizeof(b), "%.2f", v / 100.0f);
  tk_->ha_aufruf("media_player.volume_set", {{"entity_id", e}, {"volume_level", b}});
  zustand_cache_[PStr(e.c_str())].pos = v;
  aktualisieren_(e);
  abf_ab_ = millis() + 1500;   // nicht gleich vom alten Stand ueberschreiben lassen
}

void Menue::player_stumm_(const std::string &e) {
  EntZustand &z = zustand_cache_[PStr(e.c_str())];
  const PStr k("is_volume_muted");
  const bool jetzt = z.attr[k] == "true";
  tk_->ha_aufruf("media_player.volume_mute", {{"entity_id", e}, {"is_volume_muted", jetzt ? "false" : "true"}});
  z.attr[k] = jetzt ? "false" : "true";   // sofort zeigen
  aktualisieren_(e);
  abf_ab_ = millis() + 1500;
}

void Menue::player_karte_(Item &it) {
  lv_obj_t *k = lv_obj_create(root_);
  lv_obj_remove_style_all(k);
  lv_obj_add_style(k, karten_stil_(), 0);
  lv_obj_add_style(k, karten_stil_gedrueckt_(), LV_STATE_PRESSED);
  lv_obj_set_style_pad_hor(k, it.teil == P_INFO ? 10 : 2, 0);
  platz_(it, k);
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(k, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  it.obj = k;
  if (it.teil == P_INFO) {
    // Kopf: Player-Name · Zustand (klein), darunter Titel (gross) und Interpret/App (grau)
    lv_obj_clear_flag(k, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(k, lv_color_hex(0x16202E), 0);
    lv_obj_set_style_border_color(k, lv_color_hex(0x16202E), 0);
    it.bg = 0x16202E;
    const int breite = (int) (it.w * zelle_) - LUECKE - 20;
    it.name = text_(k, it.j["label"] | "", f_klein_, C_GRAU);
    lv_label_set_long_mode(it.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(it.name, breite);
    lv_obj_align(it.name, LV_ALIGN_TOP_LEFT, 0, 0);
    it.wert = text_(k, "", f_text_, C_TEXT);
    lv_label_set_long_mode(it.wert, LV_LABEL_LONG_DOT);
    lv_obj_set_width(it.wert, breite);
    lv_obj_align(it.wert, LV_ALIGN_LEFT_MID, 0, 2);
    it.zusatz = text_(k, "", f_klein_, C_GRAU);
    lv_label_set_long_mode(it.zusatz, LV_LABEL_LONG_DOT);
    lv_obj_set_width(it.zusatz, breite);
    lv_obj_align(it.zusatz, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return;
  }
  JsonArrayConst ws = it.j["werte"];
  if (it.teil == P_PRESET && ws.size() >= 2) {
    // Preset-Leiste: mehrere Werte nebeneinander in einer Kachel, jeder antippbar, der aktuelle gruen
    lv_obj_set_style_pad_all(k, 3, 0);
    lv_obj_set_layout(k, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(k, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(k, 3, 0);
    const int idx = (int) (&it - items_.data());
    int n = 0;
    for (JsonVariantConst v : ws) {
      if (n >= 6) break;
      lv_obj_t *b = lv_obj_create(k);
      lv_obj_remove_style_all(b);
      lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(b, lv_color_hex(0x232A35), 0);
      lv_obj_set_style_bg_color(b, lv_color_hex(hl_), LV_STATE_PRESSED);
      lv_obj_set_style_radius(b, 7, 0);
      lv_obj_set_height(b, LV_PCT(100));
      lv_obj_set_flex_grow(b, 1);
      char t[8];
      snprintf(t, sizeof(t), "%d", v.as<int>());
      lv_obj_center(text_(b, t, f_text_, C_TEXT));
      lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(b, (void *) (intptr_t) (idx * 8 + n));
      lv_obj_add_event_cb(b, Menue::seg_cb_, LV_EVENT_CLICKED, this);
      n++;
    }
    return;
  }
  // Taste/Preset: Symbol (eigenes "icon" oder passend zur Funktion) und optional Beschriftung - gross genug:
  // Symbol oben, Text darunter; breit: nebeneinander. Preset ohne Symbol: "N %" (oder die Beschriftung).
  const char *label = it.j["label"] | "";
  char pz[8];
  if (it.teil == P_PRESET && !*label) { snprintf(pz, sizeof(pz), "%u %%", (unsigned) it.preset); label = pz; }
  const char *ic = it.j["icon"] | "";
  if (!*ic && it.teil != P_PRESET) {
    switch (it.teil) {
      case P_MUTE: ic = "volume-high"; break;
      case P_PREV: ic = "skip-previous"; break;
      case P_NEXT: ic = "skip-next"; break;
      case P_PLAYNUR: ic = "play"; break;
      case P_PAUSE: ic = "pause"; break;
      case P_STOP: ic = "stop"; break;
      case P_VOLUP: ic = "volume-plus"; break;
      case P_VOLDN: ic = "volume-minus"; break;
      case P_SHUFFLE: ic = "shuffle"; break;
      case P_REPEAT: ic = "repeat"; break;
      case P_POWER: ic = "power"; break;
      default: ic = "play-pause";
    }
  }
  const char *glyph = *ic ? icon_(ic) : nullptr;
  if (glyph) it.ico = text_(k, glyph, f_icon_ ? f_icon_ : f_text_, C_TEXT);
  if (*label) it.name = text_(k, label, glyph ? f_klein_ : f_text_, glyph ? C_GRAU : C_TEXT);
  if (it.ico && it.name) {
    if (it.h >= 2 || it.w <= 2) {
      lv_obj_align(it.ico, LV_ALIGN_CENTER, 0, it.h >= 2 ? -8 : -6);
      lv_obj_align(it.name, LV_ALIGN_BOTTOM_MID, 0, it.h >= 2 ? -2 : 1);
      if (it.h < 2) lv_obj_set_style_text_font(it.name, f_xs_ ? f_xs_ : f_klein_, 0);
    } else {
      lv_obj_align(it.ico, LV_ALIGN_LEFT_MID, 4, 0);
      lv_obj_align(it.name, LV_ALIGN_LEFT_MID, 32, 0);
    }
    lv_label_set_long_mode(it.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(it.name, LV_SIZE_CONTENT);
  } else if (it.ico) {
    lv_obj_center(it.ico);
  } else if (it.name) {
    lv_obj_center(it.name);
  }
}

void Menue::player_zustand_(Item &it) {
  const char *e = it.j["entity"] | "";
  auto f = zustand_cache_.find(PStr(e));
  if (f == zustand_cache_.end()) {
    if (it.teil == P_INFO && it.wert) lv_label_set_text(it.wert, "–");
    return;
  }
  const EntZustand &z = f->second;
  auto attr = [&](const char *a) -> std::string {
    auto g = z.attr.find(PStr(a));
    return g == z.attr.end() ? std::string() : std::string(g->second.c_str());
  };
  const bool stumm = attr("is_volume_muted") == "true";
  switch (it.teil) {
    case P_INFO: {
      static const char *const DE[][2] = {{"playing", "spielt"}, {"paused", "pausiert"}, {"idle", "bereit"},
                                          {"off", "aus"}, {"on", "an"}, {"standby", "Standby"}, {"buffering", "lädt"},
                                          {"unavailable", "nicht da"}, {"unknown", "–"}};
      const char *st = z.state.c_str();
      for (auto &d : DE) if (z.state == d[0]) { st = d[1]; break; }
      std::string kopf = it.j["label"] | "";
      if (!kopf.empty()) kopf += " · ";
      kopf += st;
      if (strcmp(lv_label_get_text(it.name), kopf.c_str()) != 0) lv_label_set_text(it.name, kopf.c_str());
      lv_obj_set_style_text_color(it.name, lv_color_hex(z.state == "playing" ? C_AN : C_GRAU), 0);
      std::string titel = attr("media_title");
      if (titel.empty()) titel = z.state == "off" ? "Ausgeschaltet" : "Nichts läuft";
      if (strcmp(lv_label_get_text(it.wert), titel.c_str()) != 0) lv_label_set_text(it.wert, titel.c_str());
      // Zweite Zeile (Konfigurator "z2"): Interpret (Standard), App, Quelle oder nichts
      const char *z2 = it.j["z2"] | "";
      const std::string wer = !strcmp(z2, "app") ? attr("app_name") : !strcmp(z2, "source") ? attr("source")
                            : !strcmp(z2, "none") ? std::string() : attr("media_artist");
      if (strcmp(lv_label_get_text(it.zusatz), wer.c_str()) != 0) lv_label_set_text(it.zusatz, wer.c_str());
      if (it.regler != nullptr) {   // Fortschrittsbalken ("bar"): Position hochgerechnet, Zeit in der Kopfzeile
        const float dur = atof(attr("media_duration").c_str());
        float pos = atof(attr("media_position").c_str());
        if (pos >= 0 && z.state == "playing") pos += (millis() - z.pos_ms) / 1000.0f;
        const bool da = dur > 0 && pos >= 0;
        lv_bar_set_value(it.regler, da ? (int32_t) std::min(1000.0f, pos * 1000.0f / dur) : 0, LV_ANIM_OFF);
        if (da) {
          char t[40];
          const int p = (int) std::min(pos, dur), d = (int) dur;
          snprintf(t, sizeof(t), " · %d:%02d / %d:%02d", p / 60, p % 60, d / 60, d % 60);
          kopf += t;
          if (strcmp(lv_label_get_text(it.name), kopf.c_str()) != 0) lv_label_set_text(it.name, kopf.c_str());
        }
      }
      return;
    }
    case P_PLAY: {
      if (*(it.j["icon"] | "")) return;   // eigenes Symbol bleibt
      const char *s = icon_(z.state == "playing" ? "pause" : "play");
      if (s && it.ico && strcmp(lv_label_get_text(it.ico), s) != 0) lv_label_set_text(it.ico, s);
      return;
    }
    case P_MUTE: {
      const char *s = *(it.j["icon"] | "") ? nullptr : icon_(stumm ? "volume-off" : "volume-high");
      if (s && it.ico && strcmp(lv_label_get_text(it.ico), s) != 0) lv_label_set_text(it.ico, s);
      farbe_(it, stumm, 0xF87171);
      return;
    }
    case P_SHUFFLE: farbe_(it, attr("shuffle") == "true", C_AN); return;
    case P_REPEAT: {
      const std::string r = attr("repeat");
      farbe_(it, r == "all" || r == "one", C_AN);
      if (it.name == nullptr && it.ico != nullptr) {   // "einmal" sichtbar machen: kleine 1 neben dem Symbol
        if (it.zusatz == nullptr) {
          it.zusatz = text_(it.obj, "1", f_xs_ ? f_xs_ : f_klein_, C_AN);
          lv_obj_align_to(it.zusatz, it.ico, LV_ALIGN_OUT_RIGHT_BOTTOM, 0, 2);
        }
        if (r == "one") lv_obj_clear_flag(it.zusatz, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(it.zusatz, LV_OBJ_FLAG_HIDDEN);
      }
      return;
    }
    case P_POWER: farbe_(it, z.state != "off" && z.state != "standby" && z.state != "unavailable", C_AN); return;
    case P_PRESET: {
      // Aktuelle Stufe gruen (auf 2 % genau - Geraete runden die Lautstaerke)
      JsonArrayConst ws = it.j["werte"];
      if (ws.size() >= 2) {
        for (uint32_t n = 0; n < lv_obj_get_child_count(it.obj) && n < ws.size(); n++) {
          lv_obj_t *b = lv_obj_get_child(it.obj, n);
          const bool an = z.pos >= 0 && std::abs(z.pos - ws[n].as<int>()) <= 2;
          lv_obj_set_style_bg_color(b, lv_color_hex(an ? 0x14532D : 0x232A35), 0);
          lv_obj_t *l = lv_obj_get_child(b, 0);
          if (l) lv_obj_set_style_text_color(l, lv_color_hex(an ? C_AN : C_TEXT), 0);
        }
        return;
      }
      farbe_(it, z.pos >= 0 && std::abs(z.pos - (int) it.preset) <= 2, C_AN);
      return;
    }
    default:
      return;
  }
}

// Zustandsfarbe einer Medien-Taste (Symbol und Text); aus = eigene Farben des Konfigurators bzw. Standard
void Menue::farbe_(Item &it, bool an, uint32_t farbe) {
  const int32_t ic = farbe_von(it.j["ic"] | ""), fg = farbe_von(it.j["fg"] | "");
  if (it.ico) lv_obj_set_style_text_color(it.ico, lv_color_hex(an ? farbe : ic >= 0 ? (uint32_t) ic : C_TEXT), 0);
  if (it.name) lv_obj_set_style_text_color(it.name, lv_color_hex(an ? farbe : fg >= 0 ? (uint32_t) fg : (it.ico ? C_GRAU : C_TEXT)), 0);
}

// ------------------------------------------------------------------ Kachel-Optionen (2026-10-26)

const EntZustand *Menue::ez_(const char *e) const {
  if (e == nullptr || !*e) return nullptr;
  auto f = zustand_cache_.find(PStr(e));
  return f == zustand_cache_.end() ? nullptr : &f->second;
}

// Text einer Quelle {"t": fester Text} | {"e": Entitaet (Standard: die eigene), "a": Attribut}
std::string Menue::quelle_(JsonObjectConst q, const Item &it) const {
  if (q.isNull()) return "";
  if (!q["t"].isNull()) return q["t"] | "";
  const char *e = q["e"] | (it.j["entity"] | ""), *a = q["a"] | "";
  const EntZustand *z = ez_(e);
  if (z == nullptr) return "";
  if (*a) {
    auto g = z->attr.find(PStr(a));
    return g == z->attr.end() ? std::string() : std::string(g->second.c_str());
  }
  std::string r = z->state.c_str();
  if (!z->unit.empty()) { r += " "; r += z->unit.c_str(); }
  return r;
}

// Einzeiliger Regler (2026-10-29): Name links, Regler in der Mitte, Wert rechts - alles senkrecht mittig
void Menue::regler_einzeilig_(Item &it, lv_obj_t *k, lv_obj_t *sl) {
  const int innen = (int) (it.w * zelle_) - LUECKE - 2 * lv_obj_get_style_pad_left(k, LV_PART_MAIN);
  const int nb = innen * 30 / 100, wb = 36;
  lv_obj_set_width(it.name, nb);
  lv_obj_align(it.name, LV_ALIGN_LEFT_MID, 0, 0);
  lv_label_set_long_mode(it.wert, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(it.wert, wb);
  lv_obj_set_style_text_align(it.wert, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(it.wert, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_width(sl, innen - nb - wb - 20);   // Knopf ragt 10 px ueber die Enden
  lv_obj_align(sl, LV_ALIGN_LEFT_MID, nb + 10, 0);
}

// Eigene Anordnungen: 2 = zweizeilig (Name oben, Wert/zweite Zeile darunter), g = grosser Wert, i = nur Symbol
void Menue::layout_eigen_(Item &it, lv_obj_t *k, const char *ic, const char *label, bool mit_wert, char lay) {
  const int innen = (int) (it.w * zelle_) - LUECKE - 20;
  const bool hoch = it.h >= 2;
  if (lay == 'i') {
    it.ico = (ic && f_icon_) ? text_(k, ic, f_icon_, C_TEXT) : text_(k, label, f_text_, C_TEXT);
    lv_obj_center(it.ico);
    if (mit_wert) { it.wert = it.ico; it.name_wert = true; }   // Zustand = Farbe des Symbols
    return;
  }
  int x = 0;
  if (lay == 'g') {
    if (ic && f_icon_) { it.ico = text_(k, ic, f_icon_, C_TEXT); lv_obj_align(it.ico, LV_ALIGN_TOP_RIGHT, 0, 0); }
    it.name = text_(k, label, f_klein_, C_GRAU);
    lv_label_set_long_mode(it.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(it.name, innen - (it.ico ? 26 : 0) - (hoch ? 0 : innen / 2));
    lv_obj_t *w = text_(k, "", hoch && f_xl_ ? f_xl_ : (f_gross_ ? f_gross_ : f_text_), C_TEXT);
    lv_label_set_long_mode(w, LV_LABEL_LONG_DOT);
    if (hoch) {
      lv_obj_align(it.name, LV_ALIGN_TOP_LEFT, 0, 0);
      lv_obj_set_width(w, innen);
      lv_obj_align(w, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    } else {   // eine Zeile: Name links, grosser Wert rechts
      lv_obj_align(it.name, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_align(w, LV_ALIGN_RIGHT_MID, it.ico ? -26 : 0, 0);
    }
    if (mit_wert) it.wert = w; else it.zusatz = w;
    return;
  }
  // zweizeilig
  if (ic && f_icon_) { it.ico = text_(k, ic, f_icon_, C_TEXT); lv_obj_align(it.ico, LV_ALIGN_LEFT_MID, 0, 0); x = 26; }
  it.name = text_(k, label, hoch ? f_text_ : f_klein_, C_TEXT);
  lv_label_set_long_mode(it.name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(it.name, innen - x);
  lv_obj_t *w = text_(k, "", f_klein_, C_GRAU);
  lv_label_set_long_mode(w, LV_LABEL_LONG_DOT);
  lv_obj_set_width(w, innen - x);
  if (hoch) {
    lv_obj_align(it.name, LV_ALIGN_LEFT_MID, x, -10);
    lv_obj_align(w, LV_ALIGN_LEFT_MID, x, 10);
  } else {
    lv_obj_align(it.name, LV_ALIGN_TOP_LEFT, x, -3);
    lv_obj_align(w, LV_ALIGN_BOTTOM_LEFT, x, 3);
  }
  if (mit_wert) it.wert = w; else it.zusatz = w;
}

// Nach Karte und Gestaltung: Rahmen, zweite Zeile, Abzeichen, langer Druck, Regler, Titel-Optionen
void Menue::extras_(Item &it) {
  if (it.obj == nullptr || it.typ == T_TEXT) return;
  JsonObjectConst j = it.j;
  const int rad = j["rad"] | -1;
  if (rad >= 0) lv_obj_set_style_radius(it.obj, rad, 0);
  const int32_t rc = farbe_von(j["rc"] | "");
  if (rc >= 0) { it.rand = rc; lv_obj_set_style_border_color(it.obj, lv_color_hex(rc), 0); }
  if (j["transp"] | false) lv_obj_set_style_bg_opa(it.obj, LV_OPA_TRANSP, 0);
  // zweite Zeile: eigene Zeile, wo kein Wert steht; sonst haengt sie am Wert ("21 °C · 45 %")
  if (!j["sub"].isNull() && it.zusatz == nullptr && it.teil == P_KEIN && it.typ != T_KAMERA &&
      (it.wert == nullptr || it.name_wert)) {
    const bool klein = it.w <= 2 && it.h < 2;
    it.zusatz = text_(it.obj, "", klein && f_xs_ ? f_xs_ : f_klein_, C_GRAU);
    lv_obj_add_flag(it.zusatz, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_label_set_long_mode(it.zusatz, LV_LABEL_LONG_DOT);
    if (klein) {
      lv_obj_set_width(it.zusatz, LV_PCT(100));
      lv_obj_set_style_text_align(it.zusatz, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(it.zusatz, LV_ALIGN_BOTTOM_MID, 0, 3);
      if (it.name) lv_obj_align(it.name, LV_ALIGN_CENTER, 0, -5);
    } else if (it.h >= 2) {
      lv_obj_set_width(it.zusatz, LV_PCT(100));
      lv_obj_set_style_text_align(it.zusatz, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(it.zusatz, LV_ALIGN_BOTTOM_MID, 0, 0);
    } else {
      lv_obj_align(it.zusatz, LV_ALIGN_RIGHT_MID, (it.typ == T_PAGE || it.typ == T_SPECIAL) ? -16 : 0, 0);
    }
  }
  if (!j["badge"].isNull()) {   // Abzeichen oben rechts (rot hinterlegt), leer = unsichtbar
    it.badge = text_(it.obj, "", f_xs_ ? f_xs_ : f_klein_, C_TEXT);
    lv_obj_add_flag(it.badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_opa(it.badge, LV_OPA_COVER, 0);
    const int32_t bf = farbe_von(j["badge"]["bg"] | "");
    lv_obj_set_style_bg_color(it.badge, lv_color_hex(bf >= 0 ? (uint32_t) bf : 0xDC2626), 0);
    lv_obj_set_style_radius(it.badge, 7, 0);
    lv_obj_set_style_pad_hor(it.badge, 4, 0);
    lv_obj_align(it.badge, LV_ALIGN_TOP_RIGHT, 8, -3);
    lv_obj_add_flag(it.badge, LV_OBJ_FLAG_HIDDEN);
  }
  if (!j["lang"].isNull() && it.fokusierbar() && !(j["hold"] | false)) {
    lv_obj_add_event_cb(it.obj, Menue::lang_cb_, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(it.obj, Menue::lang_cb_, LV_EVENT_LONG_PRESSED, this);
  }
  if (it.regler != nullptr && (it.typ == T_ZAHL || it.typ == T_LIGHT)) {
    const int32_t rf = farbe_von(j["rf"] | "");
    if (rf >= 0) lv_obj_set_style_bg_color(it.regler, lv_color_hex(rf), LV_PART_INDICATOR);
    if (j["live"] | false) lv_obj_add_event_cb(it.regler, Menue::live_cb_, LV_EVENT_VALUE_CHANGED, this);
  }
  if (it.teil == P_INFO) {
    if (j["lauf"] | false) lv_label_set_long_mode(it.wert, LV_LABEL_LONG_SCROLL_CIRCULAR);   // Laufschrift
    if (j["bar"] | false) {
      lv_obj_t *b = lv_bar_create(it.obj);
      lv_obj_set_size(b, LV_PCT(100), 3);
      lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 2);
      lv_bar_set_range(b, 0, 1000);
      lv_obj_set_style_bg_color(b, lv_color_hex(0x2E3643), LV_PART_MAIN);
      const int32_t rf = farbe_von(j["rf"] | "");
      lv_obj_set_style_bg_color(b, lv_color_hex(rf >= 0 ? (uint32_t) rf : hl_), LV_PART_INDICATOR);
      it.regler = b;
      if (it.zusatz) lv_obj_align(it.zusatz, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    }
  }
}

// Zustand: Wert umbenennen/formatieren, zweite Zeile, Abzeichen, Stil je Zustand, Bedingung
void Menue::extras_zustand_(Item &it, std::string &wert) {
  JsonObjectConst j = it.j;
  const EntZustand *z = ez_(j["entity"] | "");
  if (z != nullptr && !z->state.empty() && it.teil == P_KEIN) {
    if (!j["nk"].isNull() || !j["eh"].isNull()) {   // Nachkommastellen / eigene Einheit (Anzeige)
      char *ende = nullptr;
      const double v = strtod(z->state.c_str(), &ende);
      const std::string eh = j["eh"].isNull() ? std::string(z->unit.c_str()) : std::string(j["eh"] | "");
      if (ende != z->state.c_str() && !j["nk"].isNull()) {
        char t[32];
        snprintf(t, sizeof(t), "%.*f", std::max(0, std::min(3, (int) (j["nk"] | 0))), v);
        wert = t;
        for (char &c : wert) if (c == '.') c = ',';
      } else if (!wert.empty()) {
        wert = z->state.c_str();
      }
      if (!wert.empty() && !eh.empty()) wert += " " + eh;
    }
    const char *m = j["map"][z->state.c_str()] | (const char *) nullptr;   // Umbenennen: {"on": "Läuft"}
    if (m != nullptr) wert = m;
  }
  if (!j["sub"].isNull() && it.teil == P_KEIN) {
    const std::string t = quelle_(j["sub"], it);
    if (it.zusatz != nullptr) { if (strcmp(lv_label_get_text(it.zusatz), t.c_str()) != 0) lv_label_set_text(it.zusatz, t.c_str()); }
    else if (!wert.empty() && !t.empty()) wert += " · " + t;
  }
  if (it.badge != nullptr) {
    const std::string t = quelle_(j["badge"], it);
    const bool weg = t.empty() || t == "0" || t == "off" || t == "unknown" || t == "unavailable";
    if (!weg && strcmp(lv_label_get_text(it.badge), t.c_str()) != 0) lv_label_set_text(it.badge, t.c_str());
    if (weg) lv_obj_add_flag(it.badge, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(it.badge, LV_OBJ_FLAG_HIDDEN);
  }
  if (!j["zst"].isNull() && z != nullptr && it.obj != nullptr) {   // Stil je Zustand: [{w, bg, fg, ic, icon}]
    JsonObjectConst t;
    for (JsonObjectConst s : j["zst"].as<JsonArrayConst>())
      if (strcasecmp(z->state.c_str(), s["w"] | "") == 0) { t = s; break; }
    const int32_t b0 = farbe_von(j["bg"] | ""), bt = t.isNull() ? -1 : farbe_von(t["bg"] | "");
    const uint32_t bg = bt >= 0 ? (uint32_t) bt : b0 >= 0 ? (uint32_t) b0 : C_KARTE;
    const bool fokus = fokus_ == (int) (&it - items_.data());
    it.bg = bg;
    if (!fokus || bg != C_KARTE) lv_obj_set_style_bg_color(it.obj, lv_color_hex(fokus && bg == C_KARTE ? C_FOKUS : bg), 0);
    if (!fokus) lv_obj_set_style_border_color(it.obj, lv_color_hex(it.rand >= 0 ? (uint32_t) it.rand : bg), 0);
    const int32_t f0 = farbe_von(j["fg"] | ""), ft = t.isNull() ? -1 : farbe_von(t["fg"] | "");
    lv_obj_t *nl = it.name_wert ? it.wert : it.name;
    if (nl) lv_obj_set_style_text_color(nl, lv_color_hex(ft >= 0 ? (uint32_t) ft : f0 >= 0 ? (uint32_t) f0 : C_TEXT), 0);
    if (it.ico) {
      const int32_t i0 = farbe_von(j["ic"] | ""), itf = t.isNull() ? -1 : farbe_von(t["ic"] | "");
      if (it.ico != nl) lv_obj_set_style_text_color(it.ico, lv_color_hex(itf >= 0 ? (uint32_t) itf : i0 >= 0 ? (uint32_t) i0 : C_TEXT), 0);
      const char *sym = (!t.isNull() && *(t["icon"] | "")) ? (t["icon"] | "") : (j["icon"] | "");
      const char *g = *sym ? icon_(sym) : nullptr;
      if (g && strcmp(lv_label_get_text(it.ico), g) != 0) lv_label_set_text(it.ico, g);
    }
  }
  if (!j["wenn"].isNull() && it.obj != nullptr) {   // Bedingung: {e, a, to, neg, art: grau|weg}
    JsonObjectConst w = j["wenn"];
    const EntZustand *wz = ez_(w["e"] | (j["entity"] | ""));
    if (wz != nullptr) {
      const char *a = w["a"] | "";
      std::string ist = wz->state.c_str();
      if (*a) { auto g = wz->attr.find(PStr(a)); ist = g == wz->attr.end() ? "" : g->second.c_str(); }
      bool ok = strcasecmp(ist.c_str(), w["to"] | "") == 0;
      if (w["neg"] | false) ok = !ok;
      if (it.gesperrt != !ok) {
        it.gesperrt = !ok;
        if (strcmp(w["art"] | "", "weg") == 0) {
          if (it.gesperrt) lv_obj_add_flag(it.obj, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(it.obj, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_set_style_opa(it.obj, it.gesperrt ? LV_OPA_40 : LV_OPA_COVER, 0);
        }
        if (it.gesperrt && fokus_ == (int) (&it - items_.data())) fokus_setzen_(-1);
      }
    }
  }
}

// Sicherheitsabfrage ("sicher"): erster Druck markiert orange ("Nochmal: ..."), erst der zweite innerhalb 3 s loest aus
bool Menue::sicher_ok_(Item &it) {
  const int idx = (int) (&it - items_.data());
  auto zurueck = [this]() {
    if (sicher_i_ < 0 || sicher_i_ >= (int) items_.size()) return;
    Item &a = items_[sicher_i_];
    lv_obj_set_style_outline_width(a.obj, 0, 0);
    if (a.name != nullptr && !a.name_wert) lv_label_set_text(a.name, a.j["label"] | "");
    sicher_i_ = -1;
  };
  if (sicher_i_ == idx && (int32_t) (millis() - sicher_bis_) < 0) {
    cancel_timeout("sicher");
    zurueck();
    return true;
  }
  zurueck();
  sicher_i_ = idx;
  sicher_bis_ = millis() + 3000;
  lv_obj_set_style_outline_width(it.obj, 3, 0);
  lv_obj_set_style_outline_color(it.obj, lv_color_hex(0xF59E0B), 0);
  lv_obj_set_style_outline_pad(it.obj, -3, 0);
  if (it.name != nullptr && !it.name_wert) lv_label_set_text(it.name, "Nochmal zum Bestätigen");
  set_timeout("sicher", 3000, zurueck);
  return false;
}

// Lautstaerke-Grenzen eines Reglers derselben Entitaet auf der Seite (vmin/vmax) gelten fuer alle Wege
int Menue::lautst_grenze_(const std::string &e, int v) const {
  for (auto &it : items_)
    if (it.typ == T_ZAHL && e == (it.j["entity"] | ""))
      v = std::max((int) (it.j["vmin"] | 0), std::min((int) (it.j["vmax"] | 100), v));
  return v;
}

void Menue::lang_cb_(lv_event_t *e) {
  auto *self = (Menue *) lv_event_get_user_data(e);
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) { self->lang_war_ = false; return; }
  const int i = (int) (intptr_t) lv_obj_get_user_data((lv_obj_t *) lv_event_get_current_target(e));
  if (i < 0 || i >= (int) self->items_.size() || self->items_[i].gesperrt) return;
  self->lang_war_ = true;
  self->fokus_setzen_(i);
  self->tk_->schritte(self->items_[i].j["lang"].as<JsonArrayConst>());
}

// Regler "live": schon beim Ziehen senden, hoechstens alle 300 ms
void Menue::live_cb_(lv_event_t *e) {
  auto *self = (Menue *) lv_event_get_user_data(e);
  lv_obj_t *sl = (lv_obj_t *) lv_event_get_target(e);
  if (!lv_obj_has_state(sl, LV_STATE_PRESSED) || millis() - self->live_ms_ < 300) return;
  self->live_ms_ = millis();
  regler_cb_(e);
}

// Preset-Leiste: ein Wert angetippt
void Menue::seg_cb_(lv_event_t *e) {
  auto *self = (Menue *) lv_event_get_user_data(e);
  const int v = (int) (intptr_t) lv_obj_get_user_data((lv_obj_t *) lv_event_get_current_target(e));
  const int i = v / 8, n = v % 8;
  if (i < 0 || i >= (int) self->items_.size()) return;
  Item &it = self->items_[i];
  if (it.gesperrt || (int) it.j["werte"].size() <= n) return;
  self->fokus_setzen_(i);
  const std::string ent = it.j["entity"] | "";
  if (ent.empty()) return;
  self->lautst_setzen_(ent, it.j["werte"][n].as<int>());
  auto &z = self->zustand_cache_[PStr(ent.c_str())];
  auto m = z.attr.find(PStr("is_volume_muted"));
  if (m != z.attr.end() && m->second == "true") self->player_stumm_(ent);
}

void Menue::ok() {
  if (fokus_ == FOKUS_ZURUECK) { if (zurueck_taste) zurueck_taste(); return; }
  if (fokus_ >= 0 && fokus_ < (int) items_.size()) ausloesen_(items_[fokus_]);
}

}  // namespace menue
}  // namespace esphome
