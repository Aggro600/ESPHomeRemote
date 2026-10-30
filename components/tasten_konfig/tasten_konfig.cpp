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
#include <strings.h>
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
#ifdef TK_USE_DIREKT
  // Antwort des Docks auf Bluetooth ueber das Dock (Platz dort nicht verbunden -> selbst senden)
  if (direkt_ != nullptr)
    direkt_->add_on_hid_antwort([this](const uint8_t *, uint8_t m, uint8_t p, bool ok) { hid_antwort_(m, p, ok); });
#endif
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
#ifdef TK_USE_DIREKT
  // Docks als HA-Link-Partner: dort langsamer Takt (Bluetooth-Adresse = WLAN-MAC + 2, s. set_link_langsam)
  if (kb_ != nullptr) {
    std::vector<std::array<uint8_t, 6>> bt;
    for (JsonPairConst kv : (*doc_)["dmac"].as<JsonObjectConst>()) {
      std::array<uint8_t, 6> m;
      if (!dock_direkt::DockDirekt::mac_lesen(kv.value() | "", m.data())) continue;
      m[5] += 2;
      bt.push_back(m);
    }
    kb_->set_link_langsam(bt);
  }
  // IP-Adressen der Docks ("dip", 2026-10-23): Heimnetz als zweiter Weg, wenn Funk (ESP-NOW) nicht durchkommt -
  // Remote und Dock an verschiedenen Zugangspunkten/Kanaelen (dock_direkt::ip_setzen)
  if (direkt_ != nullptr) {
    for (JsonPairConst kv : (*doc_)["dip"].as<JsonObjectConst>()) {
      uint8_t mac[6];
      if (dock_direkt::DockDirekt::mac_lesen((*doc_)["dmac"][kv.key().c_str()] | "", mac)) direkt_->ip_setzen(mac, kv.value() | "");
    }
  }
#endif
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
  if (einst_.empty() || !einst_melden) return;
  // Nach jedem (Wieder-)Verbinden mit HA alles neu melden (2 s spaeter): brach die API-Sitzung direkt nach dem Start
  // wieder ab, ging die einzige Meldung verloren und HA kannte neue Einstellungen nie (2026-10-20, "begruessung").
  // Erst wenn HA die Zustaende abonniert hat (2026-10-21): ueber Bluetooth stand die Verbindung oft schon, HA hoerte
  // aber noch nicht zu - das Ereignis kam 2 s nach dem Verbinden und ging verloren (Remote 2/Remote 1 meldeten nie).
  const bool api = api::global_api_server != nullptr && api::global_api_server->is_connected_with_state_subscription();
  if (api && !einst_api_vorher_) { einst_voll_ = true; einst_ms_ = millis(); }
  einst_api_vorher_ = api;
  if (millis() - einst_ms_ < 2000) return;
  einst_ms_ = millis();
  bool anders = einst_voll_;
  for (auto &f : einst_) if (f.get() != f.gemeldet) { anders = true; break; }
  if (!anders) return;
  if (!api) {
#ifdef TK_USE_DIREKT
    // Ohne eigene HA-Verbindung ueber das Dock (2026-10-25): nur die geaenderten Werte - der volle Stand (~1,4 kB)
    // passt nicht in ein Funkpaket. HA sortiert sie in den bekannten Stand ein ("teil"). Vorher blieb es hier haengen.
    if (millis() - einst_dock_ms_ > 30000 || einst_dock_ms_ == 0) {
      JsonDocument t(&alloc_);
      for (auto &f : einst_)
        if (f.get() != f.gemeldet) {
          if (f.bol) t[f.k] = f.get() != 0;
          else t[f.k] = f.get();
        }
      std::string ts;
      serializeJson(t, ts);
      if (t.size() > 0 && ts.size() < 900) {
        einst_dock_ms_ = millis() | 1;
        if (dock_aufruf("esphomeremote_konfig.einst_melden", {{"hash", hash_}, {"werte", ts}, {"teil", "1"}})) {
          for (auto &f : einst_) f.gemeldet = f.get();
          ESP_LOGI(TAG, "%u Einstellungen ueber das Dock an HA gemeldet (%u Byte)", (unsigned) t.size(), (unsigned) ts.size());
          return;
        }
      }
    }
#endif
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
  if (teil_buf_ != nullptr && (teil_hash_ != dl_soll_ || teil_len_ == 0)) { free(teil_buf_); teil_buf_ = nullptr; teil_len_ = 0; }
  dl_ab_ = teil_buf_ != nullptr ? teil_len_ : 0;
  if (dl_ab_ > 0) {
    dl_url_ += "&ab=" + std::to_string(dl_ab_) + "&h=" + dl_soll_;
    ESP_LOGI(TAG, "Belegung %s: weiter ab Byte %u", dl_soll_.c_str(), (unsigned) dl_ab_);
  }
  dl_voll_ = -1;
  dl_fertig_ = false;
  dl_laeuft_ = true;
  dl_start_ = millis();
  dl_gelesen_ = 0;
  dl_gesamt_ = -1;
  letzt_pct_ = -2;
  versuch_++;
  ESP_LOGI(TAG, "Hole Belegung %s (Versuch %u)", dl_soll_.c_str(), (unsigned) versuch_);
  dl_gelesen_ = dl_ab_;
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
    status = esp_http_client_get_status_code(c);
    ESP_LOGD(TAG, "HTTP %d, Laenge %d", status, (int) cl);
    // 206 = Rest ab dl_ab_ an den liegenden Puffer haengen; 200 = ganz (HA hat inzwischen anders gespeichert o. ae.)
    size_t ab = 0;
    if (status == 206 && self->teil_buf_ != nullptr && self->dl_ab_ > 0) {
      buf = self->teil_buf_;
      self->teil_buf_ = nullptr;
      ab = len = self->dl_ab_;
      status = 200;
    } else if (status == 200) {
      buf = (char *) heap_caps_malloc(MAX_BYTES, MALLOC_CAP_SPIRAM);
    }
    self->dl_voll_ = cl > 0 ? (int) (ab + cl) : -1;
    self->dl_gesamt_ = self->dl_voll_;
    self->dl_gelesen_ = len;
    while (status == 200 && buf != nullptr && len < MAX_BYTES) {
      int r = esp_http_client_read(c, buf + len, MAX_BYTES - len);
      if (r <= 0) break;
      len += r;
      self->dl_gelesen_ = len;
      if (cl > 0 && len >= ab + (size_t) cl) break;
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
  ble_puffer_();
  // Bluetooth ueber das Dock: gehaltene Taste am Leben halten (das Dock laesst sonst nach 1,2 s los), Touchpad
  if (dock_halt_.an && (int32_t) (millis() - dock_halt_.naechst) >= 0) {
    dock_halt_.naechst = millis() + 400;
    ble_dock_(dock_halt_.dock, dock_halt_.platz, dock_halt_.art, 3, dock_halt_.code, dock_halt_.mod, {}, {});
  }
  if (maus_.offen) maus_pumpen_();
  if (modus_ >= 0 && ((int32_t) (millis() - modus_bis_) >= 0 || modus_gen_ != gen_)) modus_beenden();
#ifdef TK_USE_DIREKT
  // Notweg vorbei, sobald das Dock wieder antwortet (2026-10-25; vorher fest 10 min Bluetooth-Werbung)
  if (notweg_bis_ != 0 && direkt_ != nullptr) {
    const uint32_t ok = direkt_->ok_ms();
    if ((int32_t) (millis() - notweg_bis_) >= 0 || (ok != 0 && (int32_t) (ok - notweg_ab_) > 500)) {
      notweg_bis_ = 0;
      ESP_LOGI(TAG, "Dock-Platz: Dock antwortet wieder - Notweg beendet");
      if (notweg_vorbei) notweg_vorbei();
    }
  }
  // Stand kurz nach einem Befehl ueber das Dock: loest er ein Popup aus (Kamera, Sleeptimer ...), kommt es gleich
  if (stand_nach_ms_ != 0 && (int32_t) (millis() - stand_nach_ms_) >= 0) {
    stand_nach_ms_ = 0;
    if (!api_ok_()) zustand_anfragen();
  }
#endif

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
    // Unvollstaendig (Verbindung brach ab): Puffer fuer den naechsten Versuch behalten, dort weitermachen
    if (dl_status_ == 200 && dl_buf_ != nullptr && dl_voll_ > 0 && dl_len_ < (size_t) dl_voll_) {
      ESP_LOGW(TAG, "Belegung %s: abgebrochen bei %u von %d Byte - naechster Versuch macht dort weiter",
               dl_soll_.c_str(), (unsigned) dl_len_, dl_voll_);
      const bool weiter = dl_len_ > dl_ab_;
      if (teil_buf_ != nullptr) free(teil_buf_);
      teil_buf_ = dl_buf_;
      teil_len_ = dl_len_;
      teil_hash_ = dl_soll_;
      dl_buf_ = nullptr;
      char g[80];
      snprintf(g, sizeof(g), "Unterbrochen bei %d %% - geht gleich weiter", (int) (dl_len_ * 100 / dl_voll_));
      melden_(4, 0, g);
      if (weiter && versuch_ > 1) versuch_ = 1;   // es geht voran: nicht als Fehlversuch zaehlen
    } else if (dl_status_ == 200 && dl_buf_ != nullptr && dl_len_ > 0) {
      const std::string h = fnv1a(dl_buf_, dl_len_);
      if (anwenden_(dl_buf_, dl_len_, h, true)) {
        // Anderer Stand als angekuendigt: HA hat inzwischen neu gespeichert - die Datei ist der aktuelle
        // Stand, nicht weiter dem alten Hash hinterherladen.
        if (h != dl_soll_ && soll_hash_ == dl_soll_) soll_hash_ = h;
        versuch_ = 0;
        erzwingen_ = false;
        if (teil_buf_ != nullptr) { free(teil_buf_); teil_buf_ = nullptr; teil_len_ = 0; }
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
  else if (!api_ok_() && wlan_holen && wlan_holen()) grund = "WLAN kommt";
  else if (!api_ok_() && !(laden_ueber_dock && laden_ueber_dock())) grund = "keine HA-Verbindung";
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
    std::vector<std::string> macs = dock_macs();   // zuletzt erreichtes, dann laut HA online, dann der Rest
    if (!macs.empty()) { ha_ueber_dock_(svc, std::move(d), std::move(macs), 0); return; }
  }
#endif
  ha_senden(svc, std::move(d));
}

bool TastenKonfig::zustand_anfragen() {
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr || doc_ == nullptr) return false;
  uint8_t mac[6];
  // Nach dem Start unbekannt bzw. das zuletzt erreichte Dock antwortet nicht: alle Docks gleich nacheinander fragen
  // (2026-10-24; vorher je Minute nur das naechste - mit zwei ausgeschalteten Docks bis zu 2 min ohne Stand)
  if (!direkt_->letztes_ok(mac)) {
    if (dock_macs().empty()) return false;
    dock_pruefen_(dock_macs(), 0);
    return true;
  }
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  const std::string zuletzt = b;
  return direkt_->zustand_anfragen(mac, App.get_name(), [this, zuletzt]() {
    std::vector<std::string> rest;
    for (auto &m : dock_macs()) if (strcasecmp(m.c_str(), zuletzt.c_str()) != 0) rest.push_back(m);
    dock_pruefen_(rest, 0);
  });
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
      stand_nach_ms_ = (millis() + 800) | 1;
      return;
    }
  }
  // WLAN verbindet sich nur gerade: kein Dock-Ausfall, der Aufruf geht gleich ueber die eigene Verbindung (2026-10-25)
  if (direkt_->funk_belegt()) ESP_LOGI(TAG, "HA-Aufruf %s: Funk belegt (WLAN verbindet sich) - direkt", svc.c_str());
  else {
    ESP_LOGI(TAG, "HA-Aufruf %s: kein Dock erreicht - direkt", svc.c_str());
    dock_fehl_ms_ = millis() | 1;
  }
#endif
  ha_senden(svc, std::move(d));
}

int TastenKonfig::dock_weg() const {
#ifdef TK_USE_DIREKT
  const int modus = ha_dock_modus ? ha_dock_modus() : 0;
  if (direkt_ == nullptr || doc_ == nullptr || (*doc_)["dmac"].as<JsonObjectConst>().size() == 0) return 0;
  // Steht die eigene HA-Verbindung, uebernimmt HA - das Dock-Symbol verschwindet (Nutzerwunsch 2026-10-20)
  if (api_ok_() || modus == 0) return 0;
  const uint32_t ok = direkt_->ok_ms();
  if (dock_fehl_ms_ != 0 && (int32_t) (dock_fehl_ms_ - ok) > 0) return 3;
  return ok != 0 ? 2 : 1;
#else
  return 0;
#endif
}

bool TastenKonfig::dock_erreichbar() const {
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr) return false;
  const uint32_t ok = direkt_->ok_ms();
  return ok != 0 && (dock_fehl_ms_ == 0 || (int32_t) (ok - dock_fehl_ms_) > 0);
#else
  return false;
#endif
}

// Dock pruefen, ohne etwas auszufuehren (2026-10-20): nur den Stand abfragen - bestaetigt das Dock, wird das
// Statusleisten-Symbol blau, sonst rot. Antwort wie bei zustand_anfragen() ueber dock_direkt on_zustand.
std::vector<std::string> TastenKonfig::dock_macs() const {
  std::vector<std::string> macs;
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr || doc_ == nullptr) return macs;
  uint8_t ok[6];
  char b[18];
  if (direkt_->letztes_ok(ok)) {
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", ok[0], ok[1], ok[2], ok[3], ok[4], ok[5]);
    macs.emplace_back(b);
  }
  // Laut HA online zuerst (2026-10-25): ausgeschaltete Docks kosteten je ~1 s, bevor das laufende drankam
  for (int runde = 0; runde < 2; runde++)
    for (JsonPairConst kv : (*doc_)["dmac"].as<JsonObjectConst>()) {
      std::string m = kv.value() | "";
      const bool online = std::find(docks_online_.begin(), docks_online_.end(), kv.key().c_str()) != docks_online_.end();
      if (runde == 0 && !online) continue;
      if (!m.empty() && std::find(macs.begin(), macs.end(), m) == macs.end()) macs.push_back(m);
    }
#endif
  return macs;
}

void TastenKonfig::dock_pruefen() {
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr || doc_ == nullptr) return;
  dock_pruefen_(dock_macs(), 0);
#endif
}

void TastenKonfig::dock_pruefen_(std::vector<std::string> macs, size_t i) {
#ifdef TK_USE_DIREKT
  uint8_t mac[6];
  while (i < macs.size() && !dock_direkt::DockDirekt::mac_lesen(macs[i].c_str(), mac)) i++;
  if (i < macs.size() &&
      direkt_->zustand_anfragen(mac, App.get_name(), [this, macs, i]() { dock_pruefen_(macs, i + 1); }))
    return;
  if (direkt_->funk_belegt()) return;   // WLAN verbindet sich: spaeter nochmal, kein Dock-Ausfall (2026-10-25)
  ESP_LOGI(TAG, "Dock-Pruefung: kein Dock erreicht");
  dock_fehl_ms_ = millis() | 1;
#endif
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
bool TastenKonfig::hat_ohne() const {
  return doc_ != nullptr && an_ && (*doc_)["ohne"].is<JsonObjectConst>();
}
int TastenKonfig::aktivitaet_slot(int i) const {
  if (doc_ == nullptr) return i;
  if (i == OHNE) return (*doc_)["ohne"]["slot"] | -1;
  if (i <= OHNE_AB) return (*doc_)["ohne"]["slot"] | aktivitaet_slot(OHNE_AB - i);
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
const char *TastenKonfig::home_wert(const char *bereich, const char *k) const {
  if (doc_ == nullptr) return "";
  return (*doc_)["home"]["areas"][bereich][k] | "";
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
  else if (!strcmp(typ, "int")) {
    if (!strcmp(s["fn"] | "", "popup_open")) { if (popup_oeffnen) popup_oeffnen(s["page"] | ""); }
    else if (intern) intern(s["fn"] | "", s["arg"] | 0, c.geste, c.dunkel);
  }
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

void TastenKonfig::medientaste(uint16_t code) {
  JsonDocument d;
  d["t"] = "ble";
  d["kind"] = "consumer";
  d["code"] = code;
  ble_(d.as<JsonObjectConst>(), false);
}

void TastenKonfig::ble_(JsonObjectConst s, bool halten) {
  auto *kb = kb_;
  const char *kind = s["kind"] | "consumer";
  const uint16_t code = s["code"] | 0;
  const uint8_t mod = s["mod"] | 0;
  int dev = s["dev"] | -1;
  if (dev >= 0 && dev == (int) kb->active_host_slot()) dev = -1;
  const uint8_t art = !strcmp(kind, "key") ? 1 : !strcmp(kind, "button") ? 2 : 0;
#ifdef TK_USE_DIREKT
  // Bluetooth ueber das Dock (2026-10-22): Weg aus dem Schritt ("w") oder der Einstellung bt_weg
  const bool nur_dock = !s["dock"].isNull();   // Geraet ist nur am Dock gekoppelt
  const int weg = s["w"].isNull() ? (bt_weg_modus ? bt_weg_modus() : 0) : (int) (s["w"] | 0);
  std::string dock;
  uint8_t platz = 0;
  const int slot = dev >= 0 ? dev : frueh_slot_ >= 0 ? frueh_slot_ : (int) kb->active_host_slot();
  if ((nur_dock || weg != 1) && bt_ziel_(s, slot, dock, platz)) {
    uint16_t conn;
    const bool selbst_da = !nur_dock && (dev >= 0 ? kb->conn_for_slot(dev, conn) : (kb->is_paired() && kb->is_connected()));
    if (nur_dock || weg == 2 || !selbst_da) {
      // Ausweichen: Dock nicht erreicht -> ueber HA ans Dock (Umweg), sonst selbst; Dock hat das Geraet gerade nicht
      // verbunden -> selbst (bei "nur am Dock" geht es dann nicht)
      auto selbst = [this, art, code, mod, dev, halten, nur_dock]() {
        if (nur_dock) { ESP_LOGW(TAG, "Bluetooth ueber das Dock: Geraet dort nicht verbunden"); return; }
        ESP_LOGI(TAG, "Bluetooth ueber das Dock ging nicht - sende selbst");
        ble_selbst_(art, code, mod, dev, halten);
      };
      auto umweg = [this, dock, platz, art, code, mod, selbst]() {
        if (api_ok_()) { ble_ha_(dock, platz, art, code, mod); return; }
        selbst();
      };
      if (ble_dock_(dock, platz, art, halten ? 1 : 0, code, mod, umweg, selbst)) {
        if (halten) dock_halt_ = DockHalt{true, dock, platz, art, mod, code, millis() + 400};
        return;
      }
      if (nur_dock) { umweg(); return; }
    }
  }
#endif
  ble_selbst_(art, code, mod, dev, halten);
}

// ---- Weck-Taste beim Start (2026-10-23)
bool TastenKonfig::frueh_geeignet_(JsonArrayConst steps, int slot) const {
  if (steps.isNull() || steps.size() == 0) return false;
  bool dock = false;
  const int weg_std = bt_weg_modus ? bt_weg_modus() : 0;
  for (JsonObjectConst s : steps) {
    const char *t = s["t"] | "";
    if (!strcmp(t, "ble")) {
      const int weg = s["w"].isNull() ? weg_std : (int) (s["w"] | 0);
      const int dev = s["dev"] | -1;
      std::string d;
      uint8_t p;
      if (!s["dock"].isNull() || (weg != 1 && bt_ziel_(s, dev >= 0 ? dev : slot, d, p))) dock = true;
    } else if (!strcmp(t, "ir")) {
      int route = doc_ != nullptr ? ((*doc_)["ir"]["route"] | 0) : 0;
      if (!s["r"].isNull()) route = s["r"] | 0;
      if (route != 0 && !docks_(s).empty()) {
        for (const auto &x : docks_(s)) if (ist_blaster(x)) return false;   // fremder Sender braucht HA
        dock = true;
      }
    } else if (!strcmp(t, "rf")) {
      for (const auto &x : docks_(s)) if (ist_blaster(x)) return false;
      dock = true;
    } else {
      return false;   // HA, Funktionen der Remote, Warten, Folgemodus: normal nach dem Start
    }
  }
  return dock;
}

bool TastenKonfig::weck_frueh(int taste, int aktivitaet, bool dunkel) {
#ifdef TK_USE_DIREKT
  if (!aktiv() || direkt_ == nullptr || taste < 0 || taste >= 110) return false;
  JsonObjectConst kc = taste_cfg_(aktivitaet, taste);
  if (kc.isNull() || hat_(kc, DOPPELT) || hat_(kc, LANG) || (kc["repeat"] | 0) > 0) return false;
  const int slot = aktivitaet_slot(aktivitaet);
  if (!frueh_geeignet_(steps_(kc, KURZ), slot)) {
    ESP_LOGI(TAG, "Weck-Taste %d: nicht ueber das Dock belegt - normal nach dem Start", taste);
    return false;
  }
  const uint32_t t0 = millis();
  frueh_slot_ = slot;
  direkt_->set_frueh(true);
  ausfuehren_(aktivitaet, taste, KURZ, dunkel);
  direkt_->pumpen(600);   // 600 ms: Dock am Repeater -> erst WLAN (~0,1 s), dann Heimnetz
  direkt_->set_frueh(false);
  frueh_slot_ = -1;
  ESP_LOGI(TAG, "Weck-Taste %d schon beim Start gesendet (%u ms nach dem Einschalten, %u ms)", taste, (unsigned) t0,
           (unsigned) (millis() - t0));
  return true;
#else
  return false;
#endif
}

void TastenKonfig::ble_selbst_(uint8_t art, uint16_t code, uint8_t mod, int dev, bool halten) {
  auto *kb = kb_;
  const bool consumer = art == 0, key = art == 1, button = art == 2;
  if (dev >= 0) {  // anderes Geraet: nur Tap, nur wenn verbunden/geparkt
    if (consumer) kb->send_consumer_to(dev, code);
    else if (key) kb->send_key_to(dev, mod, code);
    else if (button) kb->send_button_to(dev, code);
    return;
  }
  // Noch nicht verbunden, kurz nach dem Start/Aufwachen: Tipp merken statt verwerfen (2026-10-20). Auch Tasten mit
  // "halten wie echte Fernbedienung" (Steuerkreuz, OK, ...) - die gehen dann als kurzer Druck raus.
  if (dev < 0 && !kb->is_paired() && millis() < 20000) {
    if (!vor_verbindung_merken_) { ESP_LOGI(TAG, "Bluetooth noch nicht verbunden - Taste verworfen (Einstellung)"); return; }
    if (ble_q_.size() < 8) ble_q_.push_back({art, code, mod});
    ESP_LOGI(TAG, "Bluetooth noch nicht verbunden - Taste gemerkt (%u)", (unsigned) ble_q_.size());
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

// ---- Bluetooth ueber das Dock (2026-10-22) ----
bool TastenKonfig::bt_ziel_(JsonObjectConst s, int slot, std::string &dock, uint8_t &platz) const {
  if (doc_ == nullptr) return false;
  if (!s.isNull() && !s["dock"].isNull()) {
    dock = s["dock"] | "";
    platz = s["dp"] | 0;
    return !dock.empty();
  }
  if (slot < 0) return false;
  char k[4];
  snprintf(k, sizeof(k), "%d", slot);
  JsonObjectConst z = (*doc_)["bt"]["dock"][k];
  if (z.isNull()) return false;
  dock = z["d"] | "";
  platz = z["p"] | 0;
  return !dock.empty();
}

bool TastenKonfig::ble_dock_(const std::string &dock, uint8_t platz, uint8_t art, uint8_t aktion, uint16_t code,
                             uint8_t mod, std::function<void()> &&kein_funk, std::function<void()> &&nicht_da) {
#ifdef TK_USE_DIREKT
  uint8_t mac[6];
  if (direkt_ == nullptr || doc_ == nullptr || !dock_direkt::DockDirekt::mac_lesen((*doc_)["dmac"][dock] | "", mac))
    return false;
  // Ereignis wie dock_hid::Ereignis: art, aktion (0 Tipp, 1 druecken, 2 loslassen, 3 halten), 4 Byte
  std::vector<uint8_t> e(6, 0);
  e[0] = art;
  e[1] = aktion;
  if (art == 1) { e[2] = mod; e[3] = code & 0xFF; }
  else { e[2] = code & 0xFF; e[3] = code >> 8; }
  // Dock nicht erreicht / Platz dort nicht verbunden: Notweg (Remote verbindet sich wieder selbst), dann wie bisher
  auto kf = [this, f = std::move(kein_funk)]() { this->notweg_(); if (f) f(); };
  auto nd = [this, f = std::move(nicht_da)]() { this->notweg_(); if (f) f(); };
  if (!direkt_->senden_hid(mac, platz, false, e, std::move(kf))) { notweg_(); return false; }
  if (aktion <= 1) {
    dock_offen_ = DockOffen{true, platz, millis(), std::move(nd)};
    stand_nach_ms_ = (millis() + 800) | 1;
  }
  if (sprache_lauschen_ms > 0) direkt_->antwort_abwarten(sprache_lauschen_ms);   // Wunsch des Fernsehers abwarten
  ESP_LOGI(TAG, "Bluetooth ueber das Dock %s, Platz %u: %s 0x%X", dock.c_str(), (unsigned) platz + 1,
           art == 1 ? "Taste" : art == 2 ? "Knopf" : "Code", (unsigned) code);
  return true;
#else
  return false;
#endif
}

void TastenKonfig::status_ueber_dock(std::map<std::string, std::string> d, bool pumpen) {
  dock_aufruf("esphomeremote_konfig.remote_status", std::move(d), pumpen);
}

bool TastenKonfig::dock_aufruf(const std::string &svc, std::map<std::string, std::string> d, bool pumpen,
                               std::function<void()> fehlschlag) {
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr || doc_ == nullptr || api_ok_()) return false;
  uint8_t mac[6];
  bool da = direkt_->letztes_ok(mac);
  JsonObjectConst dm = (*doc_)["dmac"].as<JsonObjectConst>();
  if (!da) {
    for (auto &m : dock_macs()) if (dock_direkt::DockDirekt::mac_lesen(m.c_str(), mac)) { da = true; break; }
  }
  if (!da) return false;
  for (JsonPairConst kv : dm) {   // Name des Docks (Anzeige in HA)
    uint8_t m[6];
    if (dock_direkt::DockDirekt::mac_lesen(kv.value() | "", m) && memcmp(m, mac, 6) == 0) d["d"] = kv.key().c_str();
  }
  d["geraet"] = App.get_name();
  std::vector<std::pair<std::string, std::string>> daten(d.begin(), d.end());
  if (direkt_->senden_ha(mac, svc, daten, std::move(fehlschlag))) {
    ESP_LOGD(TAG, "%s ueber das Dock (%u Werte)", svc.c_str(), (unsigned) daten.size());
    if (pumpen) direkt_->pumpen(300);
    return true;
  }
#endif
  return false;
}

bool TastenKonfig::platz_ueber_dock(int platz) const {
  if (doc_ == nullptr || platz < 0 || (notweg_bis_ != 0 && (int32_t) (millis() - notweg_bis_) < 0)) return false;
  const int weg = bt_weg_modus ? bt_weg_modus() : 0;
  std::string dock;
  uint8_t dp = 0;
  return weg == 2 && bt_ziel_(JsonObjectConst(), platz, dock, dp);
}

void TastenKonfig::notweg_() {
  // Auch wenn der Dock-Platz noch gar nicht aktiv ist (Weck-Taste beim Start, 2026-10-23): sonst schaltet ihn
  // apply_activity danach ein, die Remote verbindet sich nie selbst und die gemerkte Taste geht verloren
  if (kb_ == nullptr) return;
#ifdef TK_USE_DIREKT
  // WLAN verbindet sich nur gerade (Funk kurz gesperrt): kein Ausfall des Docks (2026-10-25)
  if (direkt_ != nullptr && direkt_->funk_belegt()) return;
#endif
  if (notweg_modus && notweg_modus() == 0) {
    ESP_LOGI(TAG, "Dock-Platz: Dock nicht erreicht - Notweg ist ausgeschaltet");
    return;
  }
  const bool neu = notweg_bis_ == 0 || (int32_t) (millis() - notweg_bis_) >= 0;
  notweg_bis_ = (millis() + 600000) | 1;
  notweg_ab_ = millis();
  if (neu) ESP_LOGW(TAG, "Dock-Platz: Dock nicht erreicht - Remote verbindet sich selbst, bis es wieder antwortet (Notweg)");
  if (kb_->hid_ruhe()) kb_->set_hid_ruhe(false);
}

// Umweg: ueber HA an das Dock (Aktion esphome.<dock>_bt_senden)
void TastenKonfig::ble_ha_(const std::string &dock, uint8_t platz, uint8_t art, uint16_t code, uint8_t mod) {
  std::map<std::string, std::string> d;
  d["platz"] = std::to_string(platz + 1);
  d["art"] = art == 1 ? "taste" : art == 2 ? "knopf" : "consumer";
  d["code"] = std::to_string(code);
  d["mod"] = std::to_string(mod);
  d["aktion"] = "tipp";
  ESP_LOGI(TAG, "Bluetooth: Dock %s direkt nicht erreicht - ueber HA", dock.c_str());
  ha_senden("esphome." + dock + "_bt_senden", std::move(d));
}

void TastenKonfig::hid_antwort_(uint8_t maske, uint8_t platz, bool ok) {
  if (ok || !dock_offen_.an || dock_offen_.platz != platz) return;
  dock_offen_.an = false;
  if (millis() - dock_offen_.ms > 1500) return;
  if (dock_halt_.an && dock_halt_.platz == platz) dock_halt_.an = false;
  ESP_LOGI(TAG, "Dock: Platz %u gerade nicht verbunden (verbunden: 0x%X)", (unsigned) platz + 1, (unsigned) maske);
  auto f = std::move(dock_offen_.nicht_da);
  dock_offen_.nicht_da = nullptr;
  if (f) f();
}

// ---- Tastatur/Text/Makros ueber das Dock (2026-10-23)
// Ueber das Dock, wenn: Weg nicht "nur Remote", der Platz hat eine "Am Dock"-Zuordnung und die Remote kann es selbst
// gerade nicht - nicht verbunden ODER ihr eigener Platz hat keine Maus/volle Tastatur (2026-10-23: Streamer an der
// Remote "Nur Tasten", am Dock "Maus + Tastatur" -> Touchpad/Text gehen ans Dock, Tasten weiter direkt).
bool TastenKonfig::eingabe_dock_ziel(std::string &dock, uint8_t &platz, bool maus) const {
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr || kb_ == nullptr) return false;
  const int weg = bt_weg_modus ? bt_weg_modus() : 0;
  const bool selbst_kann = kb_->is_paired() && kb_->is_connected() && (maus ? kb_->maus_aktiv() : kb_->tastatur_voll());
  return weg != 1 && bt_ziel_(JsonObjectConst(), kb_->active_host_slot(), dock, platz) && (weg == 2 || !selbst_kann);
#else
  return false;
#endif
}

bool TastenKonfig::sprache_ziel() {
  sprache_da_ = false;
#ifdef TK_USE_DIREKT
  std::string dock;
  uint8_t platz = 0;
  if (doc_ == nullptr || !eingabe_dock_ziel(dock, platz)) return false;
  if (!dock_direkt::DockDirekt::mac_lesen((*doc_)["dmac"][dock] | "", sprache_mac_)) return false;
  sprache_platz_ = platz;
  sprache_da_ = true;
  ESP_LOGI(TAG, "Sprache ueber das Dock %s, Platz %u", dock.c_str(), (unsigned) platz + 1);
#endif
  return sprache_da_;
}

bool TastenKonfig::sprache_senden(const std::vector<uint8_t> &paket) {
#ifdef TK_USE_DIREKT
  if (!sprache_da_ && !sprache_ziel()) return false;
  return direkt_->senden_sprache(sprache_mac_, sprache_platz_, paket, {});
#else
  return false;
#endif
}

bool TastenKonfig::eingabe_ueber_dock(uint8_t art, uint16_t code, uint8_t mod, std::function<void()> &&selbst) {
  std::string dock;
  uint8_t platz = 0;
  if (!eingabe_dock_ziel(dock, platz)) return false;
  auto zweit = selbst;
  return ble_dock_(dock, platz, art, 0, code, mod, std::move(selbst), std::move(zweit));
}

bool TastenKonfig::text_ueber_dock(const std::string &text, std::function<void()> &&selbst) {
#ifdef TK_USE_DIREKT
  std::string dock;
  uint8_t platz = 0;
  uint8_t mac[6];
  if (!eingabe_dock_ziel(dock, platz) || doc_ == nullptr ||
      !dock_direkt::DockDirekt::mac_lesen((*doc_)["dmac"][dock] | "", mac))
    return false;
  const auto tasten = kb_->text_tasten(text.size() > 400 ? text.substr(0, 400) : text);
  if (tasten.empty()) return true;
  // Je Paket hoechstens 100 Anschlaege (600 Byte); das Dock reiht sie je Platz hintereinander (Tipp = 30 ms)
  auto geteilt = std::make_shared<std::function<void()>>(std::move(selbst));
  auto einmal = std::make_shared<bool>(false);
  auto ausweg = [geteilt, einmal]() { if (!*einmal && *geteilt) { *einmal = true; (*geteilt)(); } };
  for (size_t ab = 0; ab < tasten.size(); ab += 100) {
    std::vector<uint8_t> e;
    for (size_t i = ab; i < tasten.size() && i < ab + 100; i++) {
      const uint8_t ev[6] = {1, 0, tasten[i].first, tasten[i].second, 0, 0};   // TASTE, TIPP, mod, code
      e.insert(e.end(), ev, ev + 6);
    }
    if (!direkt_->senden_hid(mac, platz, false, e, ausweg)) {
      if (ab == 0) return false;
      ausweg();
      break;
    }
  }
  dock_offen_ = DockOffen{true, platz, millis(), ausweg};
  ESP_LOGI(TAG, "Text ueber das Dock %s, Platz %u: %u Anschlaege", dock.c_str(), (unsigned) platz + 1, (unsigned) tasten.size());
  return true;
#else
  return false;
#endif
}

// Touchpad ueber das Dock: Bewegungen sammeln und hoechstens ein Paket gleichzeitig unterwegs (ESP-NOW mit Quittung,
// ~2-5 ms) - so laeuft der Zeiger fluessig, ohne dass sich Pakete stauen.
bool TastenKonfig::maus_ueber_dock(uint8_t knoepfe, int8_t dx, int8_t dy, int8_t rad) {
#ifdef TK_USE_DIREKT
  if (direkt_ == nullptr || kb_ == nullptr) return false;
  // Ziel alle 500 ms neu bestimmen (Aktivitaet/Verbindung kann wechseln)
  if (millis() - maus_.ziel_ms > 500) {
    maus_.ziel_ms = millis();
    std::string dock;
    uint8_t platz = 0;
    maus_.ziel = eingabe_dock_ziel(dock, platz, true);
    if (maus_.ziel && (dock != maus_.dock || platz != maus_.platz)) { maus_.dock = dock; maus_.platz = platz; maus_.offen = false; }
  }
  if (!maus_.ziel) return false;
  // Knopfwechsel (Klick) nie mit anderen zusammenfassen: Bisheriges sofort raus, dann der neue Stand
  if (knoepfe != maus_.knoepfe && maus_.offen) maus_pumpen_(true);
  maus_.dx += dx; maus_.dy += dy; maus_.rad += rad;
  const bool klick = knoepfe != maus_.gesendet;
  maus_.knoepfe = knoepfe;
  maus_.offen = true;
  maus_pumpen_(klick);
  return true;
#else
  return false;
#endif
}

void TastenKonfig::maus_pumpen_(bool sofort) {
#ifdef TK_USE_DIREKT
  if (!maus_.offen || direkt_ == nullptr || (!sofort && direkt_->warteschlange() > 0)) return;
  uint8_t mac[6];
  if (doc_ == nullptr || !dock_direkt::DockDirekt::mac_lesen((*doc_)["dmac"][maus_.dock] | "", mac)) { maus_.offen = false; return; }
  auto klemmen = [](int16_t &v) -> int8_t { const int16_t x = v > 127 ? 127 : v < -127 ? -127 : v; v -= x; return (int8_t) x; };
  std::vector<uint8_t> e(6, 0);
  e[0] = 4;   // dock_hid::MAUS
  e[2] = maus_.knoepfe;
  e[3] = (uint8_t) klemmen(maus_.dx);
  e[4] = (uint8_t) klemmen(maus_.dy);
  e[5] = (uint8_t) klemmen(maus_.rad);
  maus_.offen = maus_.dx != 0 || maus_.dy != 0 || maus_.rad != 0;
  maus_.gesendet = maus_.knoepfe;
  // Kein Ausweichen fuer einzelne Bewegungen - verloren ist verloren, die naechste kommt gleich
  direkt_->senden_hid(mac, maus_.platz, false, e, {});
#endif
}

void TastenKonfig::ble_puffer_() {
  if (ble_q_.empty() || kb_ == nullptr) return;
  if (millis() > 30000) { ESP_LOGW(TAG, "%u gemerkte Tasten verworfen (keine Bluetooth-Verbindung)", (unsigned) ble_q_.size()); ble_q_.clear(); return; }
  if (!kb_->is_paired() || !kb_->is_connected()) { ble_da_ms_ = 0; return; }
  if (ble_da_ms_ == 0) { ble_da_ms_ = millis(); return; }
  if (millis() - ble_da_ms_ < 300) return;   // Host erst die Benachrichtigungen einschalten lassen
  const BleJob j = ble_q_.front();
  ble_q_.erase(ble_q_.begin());
  ESP_LOGI(TAG, "Gemerkte Taste nachgesendet (noch %u)", (unsigned) ble_q_.size());
  if (j.art == 0) kb_->send_consumer(j.code);
  else if (j.art == 1) kb_->send_key_combo(j.mod, j.code);
  else kb_->send_button(j.code);
  if (kb_->has_held() && !haelt_irgendwas_()) kb_->execute_action("release");
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
      auto ueber_ha = [this, dock, t, f, nur_dock, selbst_gemacht]() {
        if (api_ok_()) {
          std::map<std::string, std::string> d;
          d["code"] = zeiten_text(t);
          d["traeger"] = std::to_string(f);
          if (ist_blaster(dock)) {   // fremder Sender (infrared.* / remote.*, 2026-10-28): nur ueber HA
            d["sender"] = dock;
            ha_senden("esphomeremote_konfig.blaster_senden", std::move(d));
          } else {
            ha_senden("esphome." + dock + "_ir_roh", std::move(d));
          }
        } else if (nur_dock && !*selbst_gemacht) {
          *selbst_gemacht = true;
          ESP_LOGI(TAG, "IR: %s weder direkt noch ueber HA erreichbar - sende selbst", dock.c_str());
          ir_selbst_(t, f);
        }
      };
      if (ist_blaster(dock)) ueber_ha();
      else an_dock_(dock, 1, f, t, std::move(ueber_ha));
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
// Eintrag der Dock-Liste ist ein fremder Sender (Entitaet "infrared.x" / "remote.x") statt eines Dock-Slugs
bool TastenKonfig::ist_blaster(const std::string &d) { return d.find('.') != std::string::npos; }

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
    // direkt per Funk, sonst ueber HA (wartet bis 10 s auf die Verbindung); fremde Sender (Broadlink) nur ueber HA
    auto ueber_ha = [this, dock, t, wdh]() {
      std::map<std::string, std::string> d;
      d["code"] = zeiten_text(t);
      d["wiederholen"] = std::to_string(wdh);
      if (ist_blaster(dock)) {
        d["sender"] = dock;
        d["art"] = "rf";
        ha_senden("esphomeremote_konfig.blaster_senden", std::move(d));
      } else {
        ha_senden("esphome." + dock + "_rf_roh", std::move(d));
      }
    };
    if (ist_blaster(dock)) ueber_ha();
    else an_dock_(dock, 2, (uint32_t) wdh, t, std::move(ueber_ha));
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
  if (aktivitaet == OHNE) return (*doc_)["ohne"]["keys"][k];
  if (aktivitaet <= OHNE_AB) {   // "Keine Aktivitaet": eigene Belegung, sonst die der zuletzt gelaufenen
    JsonObjectConst o = (*doc_)["ohne"]["keys"][k];
    if (!o.isNull()) return o;
    aktivitaet = OHNE_AB - aktivitaet;
  }
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
      if (strcmp(t, "int") == 0 && (strncmp(fn, "menu", 4) == 0 || strcmp(fn, "display_wake") == 0 || strcmp(fn, "popup_open") == 0 ||
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
      if (dock_halt_.an) {   // ueber das Dock gehalten: dort loslassen
        dock_halt_.an = false;
        ble_dock_(dock_halt_.dock, dock_halt_.platz, dock_halt_.art, 2, dock_halt_.code, dock_halt_.mod, {}, {});
      }
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
