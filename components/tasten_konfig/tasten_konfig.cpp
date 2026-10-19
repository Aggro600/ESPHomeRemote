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
#include "ir_encoders.h"
#ifdef TK_USE_IR
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#endif
#ifdef TK_USE_DIREKT
#include "esphome/components/dock_direkt/dock_direkt.h"
#endif
#include <memory>

namespace esphome {
namespace tasten_konfig {

static const char *const TAG = "tasten_konfig";
static const size_t MAX_BYTES = 96 * 1024;   // HA begrenzt auf 90000 Byte (esphomeremote_konfig)
static const char *const NVS_NS = "tasten";

static std::string fnv1a(const char *d, size_t n) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < n; i++) { h ^= (uint8_t) d[i]; h *= 0x01000193u; }
  char b[9];
  snprintf(b, sizeof(b), "%08x", (unsigned) h);
  return b;
}

// Rohzeiten als Text "9000,-4500,..." fuer die Docks (Aktionen esphome.<dock>_ir_roh / _rf_roh)
static std::string zeiten_text(const IrTimings &t) {
  std::string s;
  s.reserve(t.size() * 6);
  for (size_t i = 0; i < t.size(); i++) {
    if (i) s += ',';
    s += std::to_string(t[i]);
  }
  return s;
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
  if (doc_ == nullptr) ESP_LOGI(TAG, "Keine gespeicherte Belegung - Tasten bedienen nur die Seiten der Remote");
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
  if (doc_ == nullptr) status_->publish_state("keine Belegung");
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
  if (speichern) einst_uebernehmen_(doc_, neu);   // beim Start aus dem NVS nicht: die Remote hat ihre Werte schon
  delete doc_;
  doc_ = neu;
  gen_++;
  hash_ = h;
  an_ = (*doc_)["on"] | true;
  ESP_LOGI(TAG, "Belegung %s geladen (%u Byte, %s, %u Aktivitaeten)", h.c_str(), (unsigned) len,
           an_ ? "AN" : "aus", (unsigned) (*doc_)["activities"].size());
  status_melden_();
  // Bluetooth-Proxys aus dem Konfigurator ("bt": {"proxies": ["AA:BB:..", ..]}) als HA-Link-Partner annehmen -
  // nur, wenn die Firmware den Link ueberhaupt hat (ha_tunnel mit festen proxy_macs), sonst bliebe er ohne Tunnel an.
  if (kb_ != nullptr && kb_->link_enabled()) {
    const JsonArrayConst proxys = (*doc_)["bt"]["proxies"].as<JsonArrayConst>();
    for (JsonVariantConst m : proxys) {
      const char *mac = m | "";
      if (strlen(mac) == 17) kb_->add_link_peer(mac);
    }
  }
  for (auto &f : neu_cbs_) f();
  return true;
}

// ------------------------------------------------------------------ Geraete-Einstellungen

void TastenKonfig::einst_uebernehmen_(JsonDocument *alt, JsonDocument *neu) {
  JsonObjectConst n = (*neu)["einst"].as<JsonObjectConst>();
  if (n.isNull() || einst_.empty()) return;
  JsonObjectConst a = alt != nullptr ? (*alt)["einst"].as<JsonObjectConst>() : JsonObjectConst();
  int geaendert = 0;
  for (auto &f : einst_) {
    JsonArrayConst w = n[f.k].as<JsonArrayConst>();
    if (w.isNull()) continue;
    JsonArrayConst wa = a.isNull() ? JsonArrayConst() : a[f.k].as<JsonArrayConst>();
    if (!wa.isNull() && (wa[1] | -1L) == (w[1] | -2L)) continue;   // im Konfigurator nicht geaendert
    const int v = w[0].is<bool>() ? (w[0].as<bool>() ? 1 : 0) : (w[0] | 0);
    if (f.get() == v) continue;
    ESP_LOGI(TAG, "Einstellung %s: %d -> %d (Konfigurator)", f.k.c_str(), f.get(), v);
    f.set(v);
    geaendert++;
  }
  if (geaendert && einst_angewendet) einst_angewendet();
}

void TastenKonfig::einst_pruefen_() {
  if (einst_.empty() || !einst_melden || millis() - einst_ms_ < 2000) return;
  einst_ms_ = millis();
  bool anders = einst_voll_;
  for (auto &f : einst_) if (f.get() != f.gemeldet) { anders = true; break; }
  if (!anders) return;
  if (!api_ok_()) {
    if (millis() - einst_log_ms_ > 60000) { einst_log_ms_ = millis(); ESP_LOGI(TAG, "Einstellungen geaendert, warte auf HA"); }
    return;
  }
  JsonDocument d(&alloc_);
  for (auto &f : einst_) {
    f.gemeldet = f.get();
    if (f.bol) d[f.k] = f.gemeldet != 0;
    else d[f.k] = f.gemeldet;
  }
  std::string s;
  serializeJson(d, s);
  einst_voll_ = false;
  ESP_LOGI(TAG, "%u Einstellungen an HA gemeldet (%u Byte)", (unsigned) einst_.size(), (unsigned) s.size());
  einst_melden(s);
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
  einst_pruefen_();
  if (modus_ >= 0 && ((int32_t) (millis() - modus_bis_) >= 0 || modus_gen_ != gen_)) modus_beenden();

  // HA-Warteschlange pumpen
  if (api_ok_()) api_da_ms_ = millis();
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
      if (anwenden_(dl_buf_, dl_len_, h, true)) {
        // Anderer Stand als angekuendigt: HA hat inzwischen neu gespeichert - die Datei ist der aktuelle
        // Stand, nicht weiter dem alten Hash hinterherladen.
        if (h != dl_soll_ && soll_hash_ == dl_soll_) soll_hash_ = h;
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
  const char *grund = nullptr;
  // Laeuft der Download noch (auch ein haengender, bis zum HTTP-Timeout): abwarten - ein zweiter Auftrag wuerde
  // Adresse und Puffer des laufenden ueberschreiben.
  if (dl_laeuft_) grund = millis() - dl_start_ > 30000 ? "Download haengt" : "Download laeuft";
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

void TastenKonfig::ha_aufruf(const std::string &svc, std::map<std::string, std::string> d) {
#ifdef TK_USE_DIREKT
  const int modus = ha_dock_modus ? ha_dock_modus() : 0;
  if (direkt_ != nullptr && doc_ != nullptr && (modus == 2 || (modus == 1 && !api_ok_()))) {
    std::vector<std::string> macs;
    uint8_t ok[6];
    char b[18];
    if (direkt_->letztes_ok(ok)) {   // das Dock, das zuletzt geantwortet hat, zuerst
      snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", ok[0], ok[1], ok[2], ok[3], ok[4], ok[5]);
      macs.emplace_back(b);
    }
    for (JsonPairConst kv : (*doc_)["dmac"].as<JsonObjectConst>()) {
      std::string m = kv.value() | "";
      if (!m.empty() && std::find(macs.begin(), macs.end(), m) == macs.end()) macs.push_back(m);
    }
    if (!macs.empty()) { ha_ueber_dock_(svc, std::move(d), std::move(macs), 0); return; }
  }
#endif
  ha_senden(svc, std::move(d));
}

bool TastenKonfig::zustand_anfragen() {
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr || doc_ == nullptr) return false;
  uint8_t mac[6];
  if (!direkt_->letztes_ok(mac)) {   // nach dem Start unbekannt: bei jedem Versuch das naechste Dock
    std::vector<std::string> macs;
    for (JsonPairConst kv : (*doc_)["dmac"].as<JsonObjectConst>()) macs.push_back(kv.value() | "");
    if (macs.empty()) return false;
    static uint32_t versuch = 0;
    if (!dock_direkt::DockDirekt::mac_lesen(macs[versuch++ % macs.size()].c_str(), mac)) return false;
  }
  return direkt_->zustand_anfragen(mac, App.get_name(), {});
#else
  return false;
#endif
}

void TastenKonfig::ha_ueber_dock_(const std::string &svc, std::map<std::string, std::string> d, std::vector<std::string> macs, size_t i) {
#ifdef TK_USE_DIREKT
  uint8_t mac[6];
  while (i < macs.size() && !dock_direkt::DockDirekt::mac_lesen(macs[i].c_str(), mac)) i++;
  if (i < macs.size()) {
    std::vector<std::pair<std::string, std::string>> daten(d.begin(), d.end());
    if (direkt_->senden_ha(mac, svc, daten, [this, svc, d, macs, i]() { ha_ueber_dock_(svc, d, macs, i + 1); })) {
      ESP_LOGI(TAG, "HA-Aufruf %s ueber das Dock %s", svc.c_str(), macs[i].c_str());
      return;
    }
  }
  ESP_LOGI(TAG, "HA-Aufruf %s: kein Dock erreicht - direkt", svc.c_str());
#endif
  ha_senden(svc, std::move(d));
}

void TastenKonfig::ha_senden(const std::string &svc, std::map<std::string, std::string> d) {
  if (api_ok_()) { kb_->call_homeassistant_service(svc, d); return; }
  if (ha_q_.size() < 8) ha_q_.push_back({svc, std::move(d), millis()});
  ESP_LOGI(TAG, "HA-Aufruf %s wartet auf die Verbindung", svc.c_str());
  if (ha_wecken) ha_wecken();
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
  else if (!strcmp(typ, "rf")) rf_(s);
  else if (!strcmp(typ, "int")) { if (intern) intern(s["fn"] | "", s["arg"] | 0, c.geste, c.dunkel); }
  else if (!strcmp(typ, "modus")) modus_starten(s["id"] | "");
}

// ------------------------------------------------------------------ Folgemodus

void TastenKonfig::modus_starten(const char *id) {
  if (doc_ == nullptr) return;
  int i = 0;
  for (JsonObjectConst m : (*doc_)["modi"].as<JsonArrayConst>()) {
    if (strcmp(m["id"] | "", id) == 0) {
      modus_ = i;
      modus_gen_ = gen_;
      modus_ms_ = (uint32_t) (m["s"] | 7) * 1000u;
      ESP_LOGI(TAG, "Folgemodus %s fuer %u s", m["name"] | id, (unsigned) (modus_ms_ / 1000));
      modus_verlaengern_();
      return;
    }
    i++;
  }
  ESP_LOGW(TAG, "Folgemodus '%s' nicht in der Konfiguration", id);
}

void TastenKonfig::modus_verlaengern_(int taste) {
  uint32_t ms = modus_ms_;
  if (taste >= 0) {   // eigene Zeit dieser Taste (Konfigurator, 2026-10-18)
    char k[12];
    snprintf(k, sizeof(k), "%d", taste);
    const uint32_t s = modus_keys_()[k]["s"] | 0u;
    if (s > 0) ms = s * 1000u;
  }
  modus_bis_ = millis() + ms;
  if (modus_anzeige) modus_anzeige((*doc_)["modi"][modus_]["name"] | "", ms);
}

void TastenKonfig::modus_beenden() {
  if (modus_ < 0) return;
  modus_ = -1;
  ESP_LOGD(TAG, "Folgemodus vorbei");
  if (modus_anzeige) modus_anzeige("", 0);
}

JsonObjectConst TastenKonfig::modus_keys_() const {
  if (modus_ < 0 || doc_ == nullptr || modus_gen_ != gen_) return JsonObjectConst();
  return (*doc_)["modi"][modus_]["keys"];
}

bool TastenKonfig::modus_taste(int taste) const {
  char k[6];
  snprintf(k, sizeof(k), "%d", taste);
  return !modus_keys_()[k].isNull();
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
  ha_aufruf(s["svc"] | "", std::move(d));
}

// IR-Schritt (2026-10-11): {"t":"ir", "proto":"NEC","addr":..,"cmd":..} oder {"t":"ir","raw":[9000,-4500,..],"f":38000}.
// Sendeweg aus der Konfiguration ("ir": {"route": 0..3, "docks": ["dock_wz"]}) wie beim OpenRemote-Original:
//   0 = nur Remote, 1 = Remote und Dock, 2 = nur Dock, 3 = Remote, danach Dock (zwei saubere Frames nacheinander).
// Das Dock bekommt die fertigen Zeiten direkt per Funk (dock_direkt, ESP-NOW, 2026-10-18) und nur, wenn es das nicht
// bestaetigt, ueber HA (Aktion esphome.<dock>_ir_roh). Geht beides nicht (oder ist kein Dock eingetragen), sendet
// die Remote selbst - eine Vorliebe fuer den Sender, kein "dann gar nicht".
void TastenKonfig::ir_(JsonObjectConst s) {
  IrTimings t;
  uint32_t f = 38000;
  JsonArrayConst raw = s["raw"];
  if (!raw.isNull()) {
    f = s["f"] | 38000u;
    t.reserve(raw.size());
    for (JsonVariantConst v : raw) {
      const int32_t us = v.as<int32_t>();
      if (us != 0) t.push_back(us);
    }
  } else {
    const std::string proto = s["proto"] | "NEC";
    if (!ir_encode_parsed(proto, s["addr"] | 0u, s["cmd"] | 0u, t, f)) {
      ESP_LOGW(TAG, "IR-Protokoll %s unbekannt", proto.c_str());
      return;
    }
  }
  if (t.empty()) return;

  int route = doc_ != nullptr ? ((*doc_)["ir"]["route"] | 0) : 0;
  if (!s["r"].isNull()) route = s["r"] | 0;   // eigener Sendeweg dieses Schritts (2026-10-11)
  const std::vector<std::string> docks = docks_(s);
  if (route == 0 || docks.empty()) {
    if (route == 2) ESP_LOGI(TAG, "IR: nur Dock gewaehlt, aber kein Dock eingetragen - sende selbst");
    ir_selbst_(t, f);
    return;
  }
  // Je Dock: direkt per Funk (2026-10-18), sonst ueber HA; ist auch HA nicht da und sollte nur das Dock senden,
  // sendet die Remote selbst (einmal, auch bei mehreren Docks).
  auto selbst_gemacht = std::make_shared<bool>(false);
  const bool nur_dock = route == 2;
  auto ueber_dock = [&]() {
    for (const auto &dock : docks) {
      an_dock_(dock, 1, f, t, [this, dock, t, f, nur_dock, selbst_gemacht]() {
        if (api_ok_()) {
          std::map<std::string, std::string> d;
          d["code"] = zeiten_text(t);
          d["traeger"] = std::to_string(f);
          ha_senden("esphome." + dock + "_ir_roh", std::move(d));
        } else if (nur_dock && !*selbst_gemacht) {
          *selbst_gemacht = true;
          ESP_LOGI(TAG, "IR: Dock weder direkt noch ueber HA erreichbar - sende selbst");
          ir_selbst_(t, f);
        }
      });
    }
  };
  if (route == 1) {   // gleichzeitig: Dock zuerst anstossen, dann selbst
    ueber_dock();
    ir_selbst_(t, f);
  } else if (route == 3) {   // selbst, danach das Dock
    ir_selbst_(t, f);
    ueber_dock();
  } else {
    ueber_dock();
  }
}

void TastenKonfig::ir_selbst_(const IrTimings &t, uint32_t f) {
#ifdef TK_USE_IR
  if (tx_ == nullptr) return;
  auto call = tx_->transmit();
  auto *d = call.get_data();
  d->set_carrier_frequency(f);
  d->reserve(t.size());
  for (int32_t v : t) { if (v > 0) d->mark(v); else d->space(-v); }
  call.perform();   // blockiert bis der Frame draussen ist
#else
  ESP_LOGW(TAG, "IR-Sender nicht eingebaut");
#endif
}

void TastenKonfig::an_dock_(const std::string &dock, int typ, uint32_t param, const IrTimings &t, std::function<void()> &&sonst) {
#ifdef TK_USE_DIREKT
  // Funk-Adressen der Docks liefert HA in der Konfiguration ("dmac": {"dock_wz": "AC:27:..."}) - bleibt im NVS,
  // also auch ohne HA bekannt.
  uint8_t mac[6];
  if (direkt_ != nullptr && doc_ != nullptr) {
    if (!dock_direkt::DockDirekt::mac_lesen((*doc_)["dmac"][dock] | "", mac))
      ESP_LOGI(TAG, "%s: Funk-Adresse unbekannt - ueber HA", dock.c_str());
    else if (direkt_->senden(mac, (dock_direkt::Typ) typ, param, t, std::move(sonst)))
      return;
  }
#endif
  if (sonst) sonst();
}

// 433-MHz-Schritt (2026-10-11): {"t":"rf","raw":[350,-1050,..],"wdh":5}. Senden kann nur das Dock (CC1101) -
// direkt per Funk (dock_direkt, 2026-10-18) oder ueber HA (Aktion esphome.<dock>_rf_roh, wartet ohne Verbindung kurz).
// Docks eines Schritts: eigene Liste "d" oder die Vorgabe "ir.docks".
std::vector<std::string> TastenKonfig::docks_(JsonObjectConst s) const {
  std::vector<std::string> v;
  JsonArrayConst a = s["d"];
  if (a.isNull() && doc_ != nullptr) a = (*doc_)["ir"]["docks"];
  for (JsonVariantConst x : a) {
    std::string d = x | "";
    if (!d.empty()) v.push_back(std::move(d));
  }
  return v;
}

// Name eines Bluetooth-Platzes aus der Konfiguration ("slots", Konfigurator > Bluetooth); leer = keiner.
std::string TastenKonfig::slot_name(int i) const {
  if (doc_ == nullptr || i < 0 || i > 3) return std::string();
  return std::string((*doc_)["slots"][i] | "");
}

int TastenKonfig::kameras() const { return doc_ != nullptr ? (int) (*doc_)["kameras"].size() : 0; }
std::string TastenKonfig::kamera_name(int i) const { return doc_ != nullptr ? std::string((*doc_)["kameras"][i]["n"] | "") : ""; }
std::string TastenKonfig::kamera_entitaet(int i) const { return doc_ != nullptr ? std::string((*doc_)["kameras"][i]["e"] | "") : ""; }
std::string TastenKonfig::ent(const char *rolle) const { return doc_ != nullptr ? std::string((*doc_)["ents"][rolle] | "") : ""; }

void TastenKonfig::rf_(JsonObjectConst s) {
  const std::vector<std::string> docks = docks_(s);
  if (docks.empty()) {
    ESP_LOGW(TAG, "433 MHz: kein Dock eingetragen (Konfigurator > Infrarot)");
    return;
  }
  IrTimings t;
  for (JsonVariantConst v : s["raw"].as<JsonArrayConst>())
    if (v.as<int32_t>() != 0) t.push_back(v.as<int32_t>());
  if (t.empty()) return;
  const int wdh = s["wdh"] | 5;
  for (const auto &dock : docks) {
    // direkt per Funk, sonst ueber HA (wartet bis 10 s auf die Verbindung)
    an_dock_(dock, 2, (uint32_t) wdh, t, [this, dock, t, wdh]() {
      std::map<std::string, std::string> d;
      d["code"] = zeiten_text(t);
      d["wiederholen"] = std::to_string(wdh);
      ha_senden("esphome." + dock + "_rf_roh", std::move(d));
    });
  }
}

void TastenKonfig::ir_test(const std::string &json) {
  JsonDocument d;
  if (deserializeJson(d, json) != DeserializationError::Ok) {
    ESP_LOGW(TAG, "ir_test: kein gueltiges JSON");
    return;
  }
  ir_(d.as<JsonObjectConst>());
}

// ------------------------------------------------------------------ Gesten

JsonObjectConst TastenKonfig::taste_cfg_(int aktivitaet, int taste) {
  if (doc_ == nullptr) return JsonObjectConst();
  char k[6];
  snprintf(k, sizeof(k), "%d", taste);
  JsonObjectConst m = modus_keys_()[k];   // Folgemodus geht vor
  if (!m.isNull()) return m;
  JsonArrayConst acts = (*doc_)["activities"];
  if (aktivitaet < 0 || aktivitaet >= (int) acts.size()) return JsonObjectConst();
  return acts[aktivitaet]["keys"][k];
}

static const char *gname(Geste g) { return g == KURZ ? "short" : g == DOPPELT ? "double" : "long"; }
static const char *gname_offline(Geste g) { return g == KURZ ? "short_offline" : g == DOPPELT ? "double_offline" : "long_offline"; }

// Schritte einer Geste. Ohne HA-Verbindung (weder WLAN noch Bluetooth-Bruecke) gilt "<geste>_offline", wenn belegt
// (Konfigurator: "Ohne Home Assistant", 2026-10-18) - sonst die normalen Schritte (HA-Aufrufe warten dann kurz).
JsonArrayConst TastenKonfig::steps_(JsonObjectConst kc, Geste g) const {
  // "Ohne HA" erst, wenn HA laenger als 15 s fehlt - nicht in den ersten Sekunden nach dem Aufwachen, waehrend die
  // Verbindung noch aufgebaut wird (2026-10-19). api_da_ms_ = zuletzt verbunden (Start zaehlt als verbunden).
  if (!api_ok_() && millis() - api_da_ms_ > 15000) {
    JsonArrayConst o = kc[gname_offline(g)];
    if (!o.isNull() && o.size() > 0) return o;
  }
  return kc[gname(g)];
}

bool TastenKonfig::hat_(JsonObjectConst kc, Geste g) const {
  JsonArrayConst a = steps_(kc, g);
  return !a.isNull() && a.size() > 0;
}

bool TastenKonfig::weckt(int taste, int aktivitaet) {
  if (!aktiv()) return false;
  JsonObjectConst kc = taste_cfg_(aktivitaet, taste);
  if (kc.isNull()) return false;
  for (const char *g : {"short", "double", "long", "short_offline", "double_offline", "long_offline"}) {
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
  if (modus_taste(taste)) modus_verlaengern_(taste);
  JsonObjectConst kc = taste_cfg_(aktivitaet, taste);
  JsonArrayConst steps = steps_(kc, g);
  ESP_LOGD(TAG, "Taste %d %s (Aktivitaet %d, %u Schritte%s)", taste,
           g == KURZ ? "KURZ" : g == DOPPELT ? "DOPPELT" : "LANG", aktivitaet, (unsigned) steps.size(),
           api_ok_() ? "" : ", ohne HA");
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

void TastenKonfig::lang_wiederholen_(int taste, uint32_t ms) {
  Zustand &z = z_[taste];
  if (!z.unten || !z.lang_fertig) return;   // losgelassen (Loslassen bricht den Zeitgeber ohnehin ab)
  ausfuehren_(z.aktivitaet, taste, LANG, z.dunkel);
  set_timeout(id_lang(taste), std::max<uint32_t>(ms, 100), [this, taste, ms]() { lang_wiederholen_(taste, ms); });
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
      for (JsonObjectConst s : steps_(kc, LANG))
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
    if (hat_(kc, DOPPELT)) {
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
  if (modus_taste(taste)) modus_verlaengern_(taste);   // bis zum Loslassen/Doppel-Fenster sicher im Modus
  JsonObjectConst kc = taste_cfg_(aktivitaet, taste);
  z.eigen = true;
  z.unten = true;
  z.aktivitaet = (int8_t) aktivitaet;
  z.lang_fertig = false;
  if (kc.isNull()) {
    ESP_LOGI(TAG, "Taste %d hat in Aktivitaet %d keine Belegung", taste, aktivitaet);
    return true;
  }
  const bool h_s = hat_(kc, KURZ), h_d = hat_(kc, DOPPELT), h_l = hat_(kc, LANG);
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
    for (JsonObjectConst s : steps_(kc, KURZ)) schritt_(s, c, true);
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
    // "Lang wiederholen" (lrepeat ms, 2026-10-15): nach der Haltezeit im Takt weiter, bis losgelassen
    const uint32_t lwdh = kc["lrepeat"] | 0;
    set_timeout(id_lang(taste), long_ms_, [this, taste, lwdh]() {
      Zustand &zz = z_[taste];
      if (!zz.unten || zz.lang_fertig) return;
      zz.lang_fertig = true;
      zz.doppel = false;
      ausfuehren_(zz.aktivitaet, taste, LANG, zz.dunkel);
      if (lwdh > 0) set_timeout(id_lang(taste), lwdh, [this, taste, lwdh]() { lang_wiederholen_(taste, lwdh); });
    });
  }
  return true;
}

}  // namespace tasten_konfig
}  // namespace esphome
