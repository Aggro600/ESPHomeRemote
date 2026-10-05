#include "tasten_konfig.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <cstring>
#include <algorithm>

#include "esphome/components/api/api_server.h"
#include "esphome/components/espidf_ble_keyboard/espidf_ble_keyboard.h"
#ifdef TK_USE_IR
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/components/runtime_config/ir_encoders.h"
#endif

namespace esphome {
namespace tasten_konfig {

static const char *const TAG = "tasten_konfig";
static const size_t MAX_BYTES = 64 * 1024;
static const char *const NVS_NS = "tasten";

static std::string fnv1a(const char *d, size_t n) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < n; i++) { h ^= (uint8_t) d[i]; h *= 0x01000193u; }
  char b[9];
  snprintf(b, sizeof(b), "%08x", (unsigned) h);
  return b;
}

// Scheduler-IDs: je Taste eigene Zeitgeber fuer "lang" und "einzeln (nach Doppel-Fenster)"
static uint32_t id_lang(int k) { return 0x7A000000u | (k << 2) | 0; }
static uint32_t id_einzeln(int k) { return 0x7A000000u | (k << 2) | 1; }

// ------------------------------------------------------------------ Laden/Speichern

void TastenKonfig::setup() {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    size_t len = 0;
    if (nvs_get_blob(h, "cfg", nullptr, &len) == ESP_OK && len > 0 && len <= MAX_BYTES) {
      char *buf = (char *) heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
      if (buf != nullptr && nvs_get_blob(h, "cfg", buf, &len) == ESP_OK) {
        anwenden_(buf, len, fnv1a(buf, len), false);
      }
      free(buf);
    }
    nvs_close(h);
  }
  if (doc_ == nullptr) ESP_LOGI(TAG, "Keine gespeicherte Belegung - eingebaute Tastenlogik aktiv");
  status_melden_();
}

void TastenKonfig::dump_config() {
  ESP_LOGCONFIG(TAG, "Tasten-Konfigurator: %s, Hash %s, lang %u ms, doppelt %u ms",
                doc_ == nullptr ? "keine Belegung" : (an_ ? "AN" : "aus"), hash_.c_str(),
                (unsigned) long_ms_, (unsigned) dbl_ms_);
}

void TastenKonfig::melden_(int phase, int pct, const std::string &text) {
  if (anzeige) anzeige(phase, pct, text.c_str());
  if (transfer_ != nullptr) {
    char b[96];
    if (phase == 2) snprintf(b, sizeof(b), "Lädt %d %% (Versuch %u)", pct < 0 ? 0 : pct, (unsigned) versuch_);
    else snprintf(b, sizeof(b), "%s", text.c_str());
    transfer_->publish_state(b);
  }
}

void TastenKonfig::status_melden_() {
  if (status_ == nullptr) return;
  if (doc_ == nullptr) status_->publish_state("eingebaut");
  else status_->publish_state(an_ ? hash_ : hash_ + " aus");
}

bool TastenKonfig::anwenden_(const char *daten, size_t len, const std::string &h, bool speichern) {
  auto *neu = new JsonDocument(&alloc_);
  DeserializationError err = deserializeJson(*neu, daten, len);
  if (err || !(*neu)["activities"].is<JsonArrayConst>()) {
    ESP_LOGE(TAG, "Belegung ungueltig (%s) - verworfen", err ? err.c_str() : "keine activities");
    delete neu;
    return false;
  }
  if (speichern) {
    nvs_handle_t nh;
    if (nvs_open(NVS_NS, NVS_READWRITE, &nh) == ESP_OK) {
      esp_err_t e = nvs_set_blob(nh, "cfg", daten, len);
      if (e == ESP_OK) e = nvs_commit(nh);
      nvs_close(nh);
      if (e != ESP_OK) ESP_LOGW(TAG, "NVS-Schreiben fehlgeschlagen: %s", esp_err_to_name(e));
    }
  }
  // Laufende Gesten/Folgen der alten Belegung beenden
  for (int k = 0; k < 110; k++) {
    if (z_[k].haelt && kb_ != nullptr) kb_->execute_action("release");
    cancel_timeout(id_lang(k));
    cancel_timeout(id_einzeln(k));
    z_[k] = Zustand{};
  }
  delete doc_;
  doc_ = neu;
  gen_++;
  hash_ = h;
  an_ = (*doc_)["on"] | true;
  ESP_LOGI(TAG, "Belegung %s geladen (%u Byte, %s, %u Aktivitaeten)", h.c_str(), (unsigned) len,
           an_ ? "AN" : "aus", (unsigned) (*doc_)["activities"].size());
  status_melden_();
  for (auto &f : neu_cbs_) f();
  return true;
}

void TastenKonfig::angekuendigt(const std::string &h) {
  if (h.size() != 8) return;  // unknown/unavailable
  if (h == hash_ && !erzwingen_) return;
  if (h != soll_hash_) { versuch_ = 0; laden_ab_ = millis(); }
  soll_hash_ = h;
  ESP_LOGI(TAG, "HA meldet Belegung %s (hier: %s)", h.c_str(), hash_.empty() ? "-" : hash_.c_str());
}

void TastenKonfig::download_starten_() {
  dl_url_ = "http://" + host_ + "/api/esphomeremote_konfig/cfg/" + App.get_name() + "?k=" + key_;
  dl_soll_ = soll_hash_;
  dl_fertig_ = false;
  dl_laeuft_ = true;
  dl_start_ = millis();
  dl_gelesen_ = 0;
  dl_gesamt_ = -1;
  letzt_pct_ = -2;
  versuch_++;
  ESP_LOGI(TAG, "Hole Belegung %s (Versuch %u)", dl_soll_.c_str(), (unsigned) versuch_);
  ESP_LOGI("ram", "vor Download: intern frei %u, groesster Block %u", (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  melden_(1, 0, "Verbinde mit Home Assistant");
  // Arbeits-Task einmalig anlegen, Stapel im PSRAM (2026-10-04): vorher bei jedem Download
  // xTaskCreate mit 6 KB internem Stapel -> Zerstueckelung des knappen internen RAM.
  if (worker_ == nullptr) {
    static StaticTask_t tcb;
    const size_t groesse = 6144;
    auto *stapel = (StackType_t *) heap_caps_malloc(groesse, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stapel != nullptr)
      worker_ = xTaskCreateStaticPinnedToCore(download_task_, "tk_dl", groesse, this, 2, stapel, &tcb, tskNO_AFFINITY);
  }
  if (worker_ != nullptr) xTaskNotifyGive((TaskHandle_t) worker_);
  else { dl_laeuft_ = false; ESP_LOGW(TAG, "Download-Task nicht gestartet"); }
}

void TastenKonfig::download_task_(void *arg) {
  auto *self = (TastenKonfig *) arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    download_einmal_(self);
  }
}

void TastenKonfig::download_einmal_(TastenKonfig *self) {
  esp_http_client_config_t cfg = {};
  cfg.url = self->dl_url_.c_str();
  cfg.timeout_ms = 5000;
  cfg.buffer_size = 1024;
  cfg.keep_alive_enable = false;
  cfg.disable_auto_redirect = true;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  int status = -1;
  size_t len = 0;
  char *buf = nullptr;
  esp_err_t oe = c != nullptr ? esp_http_client_open(c, 0) : ESP_FAIL;
  if (oe != ESP_OK) ESP_LOGW(TAG, "Verbindung zu HA fehlgeschlagen: %s", esp_err_to_name(oe));
  if (oe == ESP_OK) {
    int64_t cl = esp_http_client_fetch_headers(c);
    self->dl_gesamt_ = cl > 0 ? (int) cl : -1;
    status = esp_http_client_get_status_code(c);
    ESP_LOGD(TAG, "HTTP %d, Laenge %d", status, (int) cl);
    if (status == 200) {
      buf = (char *) heap_caps_malloc(MAX_BYTES, MALLOC_CAP_SPIRAM);
      while (buf != nullptr && len < MAX_BYTES) {
        int r = esp_http_client_read(c, buf + len, MAX_BYTES - len);
        if (r <= 0) break;
        len += r;
        self->dl_gelesen_ = len;
        if (cl > 0 && len >= (size_t) cl) break;
      }
    }
  }
  if (c != nullptr) { esp_http_client_close(c); esp_http_client_cleanup(c); }
  self->dl_buf_ = buf;
  self->dl_len_ = len;
  self->dl_status_ = status;
  self->dl_fertig_ = true;
}

void TastenKonfig::loop() {
  // HA-Warteschlange pumpen
  if (!ha_q_.empty()) {
    const bool con = api_ok_();
    for (auto it = ha_q_.begin(); it != ha_q_.end();) {
      if (con) { kb_->call_homeassistant_service(it->svc, it->d); it = ha_q_.erase(it); }
      else if (millis() - it->t0 > 10000) { ESP_LOGW(TAG, "HA-Aufruf %s verworfen (keine Verbindung)", it->svc.c_str()); it = ha_q_.erase(it); }
      else ++it;
    }
  }

  // Fortschritt melden (hoechstens alle 250 ms)
  if (dl_laeuft_ && !dl_fertig_ && millis() - letzt_melden_ > 250) {
    letzt_melden_ = millis();
    const int ges = dl_gesamt_;
    const int pct = ges > 0 ? (int) (dl_gelesen_ * 100 / ges) : -1;
    if (pct != letzt_pct_) { letzt_pct_ = pct; melden_(2, pct, ""); }
  }

  // Download fertig?
  if (dl_fertig_) {
    dl_fertig_ = false;
    dl_laeuft_ = false;
    if (dl_status_ == 200 && dl_buf_ != nullptr && dl_len_ > 0) {
      const std::string h = fnv1a(dl_buf_, dl_len_);
      if (h != dl_soll_) {
        // HA hat inzwischen neu gespeichert (Ankuendigung verpasst/ueberholt): die Datei ist der
        // aktuelle Stand - uebernehmen und nicht weiter dem alten Hash hinterherladen.
        ESP_LOGI(TAG, "Neuerer Stand als angekuendigt (erwartet %s, erhalten %s) - uebernommen", dl_soll_.c_str(), h.c_str());
        if (anwenden_(dl_buf_, dl_len_, h, true)) {
          if (soll_hash_ == dl_soll_) soll_hash_ = h;
          versuch_ = 0;
          erzwingen_ = false;
          melden_(3, 100, "Übernommen (" + h + ")");
        } else {
          melden_(4, 0, "Fehler: ungültige Daten");
        }
      } else if (anwenden_(dl_buf_, dl_len_, h, true)) {
        ESP_LOGI("ram", "nach Laden: intern frei %u, groesster Block %u", (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        versuch_ = 0;
        erzwingen_ = false;
        melden_(3, 100, "Übernommen (" + h + ")");
      } else {
        melden_(4, 0, "Fehler: ungültige Daten");
      }
    } else {
      ESP_LOGW(TAG, "Download fehlgeschlagen (HTTP %d, %u Byte)", dl_status_, (unsigned) dl_len_);
      char g[80];
      if (dl_status_ < 0) snprintf(g, sizeof(g), "Fehler: keine Verbindung zu HA (Versuch %u von 8)", (unsigned) versuch_);
      else if (dl_status_ == 403) snprintf(g, sizeof(g), "Fehler: Schlüssel falsch (HTTP 403)");
      else snprintf(g, sizeof(g), "Fehler: HTTP %d (Versuch %u von 8)", dl_status_, (unsigned) versuch_);
      melden_(4, 0, g);
    }
    free(dl_buf_);
    dl_buf_ = nullptr;
    laden_ab_ = millis() + (versuch_ < 3 ? 5000 : 60000);
  }

  // Neu laden noetig?
  const bool abweichung = !soll_hash_.empty() && (soll_hash_ != hash_ || erzwingen_);
  if (!abweichung) return;
  if (dl_laeuft_ && millis() - dl_start_ > 30000) {
    ESP_LOGW(TAG, "Download haengt seit 30 s - gebe ihn auf");
    dl_laeuft_ = false;  // Task laeuft evtl. noch; sein Ergebnis wird ueber dl_fertig_ trotzdem verarbeitet
  }
  const char *grund = nullptr;
  if (dl_laeuft_) grund = "Download laeuft";
  else if (versuch_ >= 8) grund = "zu viele Versuche";
  else if ((int32_t) (millis() - laden_ab_) < 0) grund = "Wartezeit";
  else if (!api_ok_()) grund = "keine HA-Verbindung";
  else if (darf_laden && !darf_laden()) { laden_ab_ = millis() + 2000; grund = "Sprache aktiv"; }
  if (grund != nullptr) {
    if (millis() - grund_log_ > 15000) { grund_log_ = millis(); ESP_LOGD(TAG, "Belegung %s ausstehend: %s", soll_hash_.c_str(), grund); }
    return;
  }
  download_starten_();
}

void TastenKonfig::ha_senden(const std::string &svc, std::map<std::string, std::string> d) {
  if (api_ok_()) { kb_->call_homeassistant_service(svc, d); return; }
  if (ha_q_.size() < 8) ha_q_.push_back({svc, std::move(d), millis()});
  ESP_LOGI(TAG, "HA-Aufruf %s wartet auf die Verbindung", svc.c_str());
}

bool TastenKonfig::popup_an(const char *name) const {
  if (doc_ == nullptr) return true;
  JsonObjectConst p = (*doc_)["popups"][name];
  return p.isNull() || (p["on"] | true);
}

int TastenKonfig::aktivitaeten() const {
  if (doc_ == nullptr || !an_) return 0;
  return (int) (*doc_)["activities"].size();
}
std::string TastenKonfig::aktivitaet_name(int i) const {
  if (doc_ == nullptr) return "";
  return (*doc_)["activities"][i]["name"] | "";
}
int TastenKonfig::aktivitaet_slot(int i) const {
  if (doc_ == nullptr) return i;
  return (*doc_)["activities"][i]["slot"] | i;
}

bool TastenKonfig::aktivitaet_im_dock(int i) const {
  if (doc_ == nullptr) return i < 4;
  return (*doc_)["activities"][i]["dock"] | (i < 4);
}

bool TastenKonfig::home_eigen() const {
  if (doc_ == nullptr) return false;
  return (*doc_)["home"]["custom"] | false;
}
bool TastenKonfig::home_bereich(const char *name, int &x, int &y, int &w, int &h, bool &an) const {
  if (!home_eigen()) return false;
  JsonObjectConst b = (*doc_)["home"]["areas"][name];
  if (b.isNull()) return false;
  x = b["x"] | 0; y = b["y"] | 0; w = b["w"] | 6; h = b["h"] | 1; an = b["on"] | true;
  return true;
}

std::string TastenKonfig::popup_ersatz(const char *name) const {
  if (doc_ == nullptr) return "";
  return (*doc_)["popups"][name]["page"] | "";
}

// ------------------------------------------------------------------ Ausfuehrung

bool TastenKonfig::api_ok_() const { return api::global_api_server != nullptr && api::global_api_server->is_connected(); }

bool TastenKonfig::haelt_irgendwas_() const {
  for (const auto &z : z_) if (z.haelt) return true;
  return false;
}

void TastenKonfig::schritt_(JsonObjectConst s, const Ctx &c, bool halten) {
  const char *typ = s["t"] | "";
  if (!strcmp(typ, "ble")) ble_(s, halten);
  else if (!strcmp(typ, "ha")) ha_(s);
  else if (!strcmp(typ, "ir")) ir_(s);
  else if (!strcmp(typ, "int")) { if (intern) intern(s["fn"] | "", s["arg"] | 0, c.geste, c.dunkel); }
}

void TastenKonfig::ble_(JsonObjectConst s, bool halten) {
  auto *kb = kb_;
  const char *kind = s["kind"] | "consumer";
  const int code = s["code"] | 0;
  const int mod = s["mod"] | 0;
  int dev = s["dev"] | -1;
  if (dev >= 0 && dev == (int) kb->active_host_slot()) dev = -1;
  const bool consumer = !strcmp(kind, "consumer"), key = !strcmp(kind, "key"), button = !strcmp(kind, "button");
  if (dev >= 0) {  // anderes Geraet: nur Tap, nur wenn verbunden/geparkt
    if (consumer) kb->send_consumer_to(dev, code);
    else if (key) kb->send_key_to(dev, mod, code);
    else if (button) kb->send_button_to(dev, code);
    return;
  }
  char act[32];
  if (halten && consumer) { snprintf(act, sizeof(act), "consumer_hold:0x%04X", code); kb->hold_action(act); return; }
  if (halten && key) { snprintf(act, sizeof(act), "key_hold:0x%02X:0x%02X", mod, code); kb->hold_action(act); return; }
  if (consumer) kb->send_consumer(code);
  else if (key) kb->send_key_combo(mod, code);
  else if (button) kb->send_button(code);
  // send_consumer stellt eine gehaltene Taste wieder her (Kanal-Fehler 2026-09-23):
  // haelt die Belegung gerade nichts, alles freigeben.
  if (kb->has_held() && !haelt_irgendwas_()) kb->execute_action("release");
}

void TastenKonfig::ha_(JsonObjectConst s) {
  std::map<std::string, std::string> d;
  for (JsonPairConst kv : s["data"].as<JsonObjectConst>()) d[kv.key().c_str()] = kv.value().as<std::string>();
  const char *e = s["entity"] | "";
  if (*e) d["entity_id"] = e;
  ha_senden(s["svc"] | "", std::move(d));
}

void TastenKonfig::ir_(JsonObjectConst s) {
#ifdef TK_USE_IR
  if (tx_ == nullptr) return;
  runtime_config::IrTimings t;
  uint32_t f = 38000;
  const std::string proto = s["proto"] | "NEC";
  if (!runtime_config::ir_encode_parsed(proto, s["addr"] | 0u, s["cmd"] | 0u, t, f)) {
    ESP_LOGW(TAG, "IR-Protokoll %s unbekannt", proto.c_str());
    return;
  }
  auto call = tx_->transmit();
  auto *d = call.get_data();
  d->set_carrier_frequency(f);
  d->reserve(t.size());
  for (int32_t v : t) { if (v > 0) d->mark(v); else d->space(-v); }
  call.perform();
#else
  ESP_LOGW(TAG, "IR nicht eingebaut");
#endif
}

// ------------------------------------------------------------------ Gesten

JsonObjectConst TastenKonfig::taste_cfg_(int aktivitaet, int taste) {
  if (doc_ == nullptr) return JsonObjectConst();
  JsonArrayConst acts = (*doc_)["activities"];
  if (aktivitaet < 0 || aktivitaet >= (int) acts.size()) return JsonObjectConst();
  char k[6];
  snprintf(k, sizeof(k), "%d", taste);
  return acts[aktivitaet]["keys"][k];
}

static bool hat(JsonObjectConst kc, const char *g) {
  JsonArrayConst a = kc[g];
  return !a.isNull() && a.size() > 0;
}

static const char *gname(Geste g) { return g == KURZ ? "short" : g == DOPPELT ? "double" : "long"; }

bool TastenKonfig::weckt(int taste, int aktivitaet) {
  if (!aktiv()) return false;
  JsonObjectConst kc = taste_cfg_(aktivitaet, taste);
  if (kc.isNull()) return false;
  for (const char *g : {"short", "double", "long"}) {
    for (JsonObjectConst s : kc[g].as<JsonArrayConst>()) {
      const char *t = s["t"] | "";
      const char *fn = s["fn"] | "";
      if (strcmp(t, "int") == 0 && (strncmp(fn, "menu", 4) == 0 || strcmp(fn, "display_wake") == 0 ||
                                    strcmp(fn, "display_off") == 0 || strcmp(fn, "home") == 0))
        return true;
    }
  }
  return false;
}

void TastenKonfig::ausfuehren_(int aktivitaet, int taste, Geste g, bool dunkel) {
  JsonObjectConst kc = taste_cfg_(aktivitaet, taste);
  JsonArrayConst steps = kc[gname(g)];
  ESP_LOGD(TAG, "Taste %d %s (Aktivitaet %d, %u Schritte)", taste,
           g == KURZ ? "KURZ" : g == DOPPELT ? "DOPPELT" : "LANG", aktivitaet, (unsigned) steps.size());
  folge_(steps, Ctx{taste, g, dunkel, aktivitaet}, 0, gen_);
}

void TastenKonfig::folge_(JsonArrayConst steps, Ctx c, size_t ab, uint32_t gen) {
  if (gen != gen_) return;  // Belegung wurde zwischendurch ersetzt (steps zeigt dann ins Leere)
  for (size_t i = ab; i < steps.size(); i++) {
    JsonObjectConst s = steps[i];
    if (strcmp(s["t"] | "", "wait") == 0) {
      const uint32_t ms = s["ms"] | 0;
      if (i + 1 < steps.size())
        set_timeout(ms, [this, steps, c, i, gen]() { folge_(steps, c, i + 1, gen); });
      return;
    }
    schritt_(s, c, false);
  }
}

void TastenKonfig::wiederholen_(int taste, uint32_t ms) {
  Zustand &z = z_[taste];
  if (!z.unten || !z.wdh) return;
  ausfuehren_(z.aktivitaet, taste, KURZ, z.dunkel);
  set_timeout(id_lang(taste), std::max<uint32_t>(ms, 50), [this, taste, ms]() { wiederholen_(taste, ms); });
}

bool TastenKonfig::ereignis(int taste, bool gedrueckt, int aktivitaet, bool dunkel, bool darf_neu) {
  if (taste < 0 || taste >= 110) return false;
  Zustand &z = z_[taste];

  if (!gedrueckt) {
    if (!z.eigen) return false;
    z.eigen = false;
    z.unten = false;
    cancel_timeout(id_lang(taste));
    const int a = z.aktivitaet;
    if (z.wdh) {
      z.wdh = false;
      return true;
    }
    if (z.haelt) {
      z.haelt = false;
      kb_->execute_action("release");
      return true;
    }
    JsonObjectConst kc = taste_cfg_(a, taste);
    if (z.lang_fertig) {
      z.lang_fertig = false;
      // Push-to-Talk: Loslassen beendet die Aufnahme
      for (JsonObjectConst s : kc["long"].as<JsonArrayConst>())
        if (strcmp(s["t"] | "", "int") == 0 && strcmp(s["fn"] | "", "voice_ptt") == 0 && intern)
          intern("voice_ptt_ende", 0, LANG, z.dunkel);
      return true;
    }
    if (z.doppel) {
      z.doppel = false;
      z.los_ms = 0;
      ausfuehren_(a, taste, DOPPELT, z.dunkel);
      return true;
    }
    if (hat(kc, "double")) {
      z.los_ms = millis();
      z.wartet = true;
      const bool d = z.dunkel;
      set_timeout(id_einzeln(taste), dbl_ms_, [this, a, taste, d]() {
        z_[taste].wartet = false;
        ausfuehren_(a, taste, KURZ, d);
      });
      return true;
    }
    ausfuehren_(a, taste, KURZ, z.dunkel);
    return true;
  }

  // ---- gedrueckt
  if (!aktiv() || !darf_neu) return false;
  JsonObjectConst kc = taste_cfg_(aktivitaet, taste);
  z.eigen = true;
  z.unten = true;
  z.aktivitaet = (int8_t) aktivitaet;
  z.lang_fertig = false;
  if (kc.isNull()) {
    ESP_LOGI(TAG, "Taste %d hat in Aktivitaet %d keine Belegung", taste, aktivitaet);
    return true;
  }
  const bool h_s = hat(kc, "short"), h_d = hat(kc, "double"), h_l = hat(kc, "long");
  // "Beim Gedrueckthalten: Kurz wiederholen" (2026-10-04): sofort einmal, nach long_ms im Takt `repeat` ms,
  // solange gehalten. Doppelt/Lang werden dabei nicht ausgewertet.
  const uint32_t wdh_ms = kc["repeat"] | 0;
  if (wdh_ms > 0 && h_s) {
    z.wdh = true;
    ausfuehren_(aktivitaet, taste, KURZ, dunkel);
    set_timeout(id_lang(taste), long_ms_, [this, taste, wdh_ms]() { wiederholen_(taste, wdh_ms); });
    return true;
  }
  if ((kc["hold"] | false) && h_s && !h_d && !h_l) {
    // Halten wie eine echte Fernbedienung: Host wiederholt selbst, Loslassen gibt frei.
    z.haelt = true;
    const Ctx c{taste, KURZ, dunkel, aktivitaet};
    for (JsonObjectConst s : kc["short"].as<JsonArrayConst>()) schritt_(s, c, true);
    return true;
  }
  // Zweiter Druck im Doppel-Fenster?
  z.doppel = h_d && z.wartet && (millis() - z.los_ms) < dbl_ms_ + 20;
  if (z.doppel) {
    cancel_timeout(id_einzeln(taste));
    z.wartet = false;
  } else {
    if (z.wartet) {  // altes Einzel-Ereignis noch offen (sehr knapp): jetzt ausloesen
      cancel_timeout(id_einzeln(taste));
      z.wartet = false;
      ausfuehren_(aktivitaet, taste, KURZ, z.dunkel);
    }
    z.dunkel = dunkel;  // beim Doppeldruck zaehlt der ERSTE Druck
  }
  if (h_l) {
    set_timeout(id_lang(taste), long_ms_, [this, taste]() {
      Zustand &zz = z_[taste];
      if (!zz.unten || zz.lang_fertig) return;
      zz.lang_fertig = true;
      zz.doppel = false;
      ausfuehren_(zz.aktivitaet, taste, LANG, zz.dunkel);
    });
  }
  return true;
}

}  // namespace tasten_konfig
}  // namespace esphome
