#include "dock_direkt.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <mbedtls/md.h>
#include <atomic>
#include <cstring>

namespace esphome {
namespace dock_direkt {

static const char *const TAG = "dock_direkt";
static const uint8_t VER = 1;
static const size_t KOPF = 2 + 1 + 1 + 4 + 4 + 4 + 2;   // bis einschliesslich anzahl
static const size_t HMAC_LEN = 8;
#ifdef ESP_NOW_MAX_DATA_LEN_V2
static const size_t MAX_PAKET = ESP_NOW_MAX_DATA_LEN_V2;
#else
static const size_t MAX_PAKET = ESP_NOW_MAX_DATA_LEN;
#endif
static const uint32_t ANTWORT_MS = 150;   // ohne Sende-Rueckmeldung: als Fehlschlag werten

// Rueckmeldungen aus dem WLAN-Task
static std::atomic<bool> s_fertig{false};
static std::atomic<bool> s_ok{false};

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void gesendet_cb(const esp_now_send_info_t *, esp_now_send_status_t st) {
#else
static void gesendet_cb(const uint8_t *, esp_now_send_status_t st) {
#endif
  s_ok = st == ESP_NOW_SEND_SUCCESS;
  s_fertig = true;
}

#ifdef DD_EMPFANG
// Empfangspuffer: der WLAN-Task schreibt, loop() liest. 3 Plaetze reichen (Tastendruecke kommen nicht schneller).
struct Fach {
  std::atomic<bool> voll{false};
  uint8_t mac[6];
  uint16_t len;
  uint8_t d[MAX_PAKET];
};
static Fach s_fach[3];
static std::atomic<uint32_t> s_verworfen{0}, s_fremd{0};

static void empfangen_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int) (KOPF + HMAC_LEN) || len > (int) MAX_PAKET || data[0] != 'O' || data[1] != 'R') { s_fremd++; return; }
  for (auto &f : s_fach) {
    if (f.voll) continue;
    memcpy(f.mac, info->src_addr, 6);
    memcpy(f.d, data, len);
    f.len = (uint16_t) len;
    f.voll = true;
    return;
  }
  s_verworfen++;
}
#endif

static void varint(std::vector<uint8_t> &o, int32_t v) {
  uint32_t z = ((uint32_t) v << 1) ^ (uint32_t) (v >> 31);
  while (z >= 0x80) { o.push_back((uint8_t) (z | 0x80)); z >>= 7; }
  o.push_back((uint8_t) z);
}
static void u32(std::vector<uint8_t> &o, uint32_t v) { for (int i = 0; i < 4; i++) o.push_back((uint8_t) (v >> (8 * i))); }
static uint32_t u32_lesen(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }

bool DockDirekt::mac_lesen(const char *s, uint8_t out[6]) {
  unsigned v[6];
  if (s == nullptr || sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t) v[i];
  return true;
}

void DockDirekt::hmac_(const uint8_t mac[6], const uint8_t *d, size_t n, uint8_t out[8]) const {
  uint8_t voll[32];
  mbedtls_md_context_t c;
  mbedtls_md_init(&c);
  mbedtls_md_setup(&c, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&c, (const unsigned char *) schluessel_.data(), schluessel_.size());
  mbedtls_md_hmac_update(&c, mac, 6);
  mbedtls_md_hmac_update(&c, d, n);
  mbedtls_md_hmac_finish(&c, voll);
  mbedtls_md_free(&c);
  memcpy(out, voll, 8);
}

void DockDirekt::setup() {
  sitzung_ = esp_random();
  kanal_pref_ = global_preferences->make_preference<uint8_t>(0x44445201u);
  if (!kanal_pref_.load(&kanal_)) kanal_ = 0;
#ifndef DD_EMPFANG
  // Laufende Nummer: ab dem vorausgespeicherten Stand weiter, naechsten Block gleich sichern
  nr_pref_ = global_preferences->make_preference<uint32_t>(0x44445202u);
  if (!nr_pref_.load(&nr_)) nr_ = 0;
  nr_bis_ = nr_ + 1000;
  nr_pref_.save(&nr_bis_);
  global_preferences->sync();   // sofort schreiben: nach einem Absturz duerfen keine Nummern doppelt vorkommen
#endif
  bereit_();
}

void DockDirekt::dump_config() {
  ESP_LOGCONFIG(TAG, "Direkter Funkweg Remote -> Dock (ESP-NOW): %s, %s, max. %u Byte, gemerkter Kanal %u",
#ifdef DD_EMPFANG
                "empfaengt",
#else
                "sendet",
#endif
                init_ ? "bereit" : "noch nicht bereit", (unsigned) MAX_PAKET, (unsigned) kanal_);
}

bool DockDirekt::bereit_() {
  if (init_) return true;
  if (millis() - init_versuch_ms_ < 2000 && init_versuch_ms_ != 0) return false;
  init_versuch_ms_ = millis() | 1;
  wifi_mode_t m;
  if (esp_wifi_get_mode(&m) != ESP_OK) return false;   // WLAN-Treiber noch nicht gestartet
  esp_err_t e = esp_now_init();
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "ESP-NOW nicht bereit: %s", esp_err_to_name(e));
    return false;
  }
  esp_now_register_send_cb(gesendet_cb);
#ifdef DD_EMPFANG
  esp_now_register_recv_cb(empfangen_cb);
#endif
  uint32_t v = 0;
  esp_now_get_version(&v);
  ESP_LOGI(TAG, "ESP-NOW bereit (Version %u)", (unsigned) v);
  init_ = true;
  return true;
}

// ------------------------------------------------------------------ Senden (Remote)

bool DockDirekt::senden(const uint8_t mac[6], Typ typ, uint32_t param, const std::vector<int32_t> &zeiten,
                        std::function<void()> &&fehlschlag) {
  if (zeiten.empty() || zeiten.size() > 0xFFFF || warte_.size() >= 6 || !bereit_()) return false;
  auto *w = wifi::global_wifi_component;
  // WLAN verbindet sich gerade (sucht Kanaele): nicht dazwischenfunken
  if (w == nullptr || !(w->is_connected() || w->is_disabled())) {
    ESP_LOGI(TAG, "Direkt geht gerade nicht (WLAN verbindet sich)");
    return false;
  }
  if (w->is_disabled() && kanal_ == 0) {   // noch nie verbunden gewesen: Kanal unbekannt
    ESP_LOGI(TAG, "Direkt geht gerade nicht (WLAN-Kanal noch unbekannt)");
    return false;
  }
  Auftrag a;
  memcpy(a.mac, mac, 6);
  auto &p = a.paket;
  p.reserve(KOPF + zeiten.size() * 2 + HMAC_LEN);
  p.push_back('O');
  p.push_back('R');
  p.push_back(VER);
  p.push_back((uint8_t) typ);
  u32(p, sitzung_);
  if (++nr_ >= nr_bis_) {   // Block aufgebraucht: naechsten sichern, bevor die Nummer benutzt wird
    nr_bis_ = nr_ + 1000;
    nr_pref_.save(&nr_bis_);
    global_preferences->sync();
  }
  u32(p, nr_);
  u32(p, param);
  p.push_back((uint8_t) (zeiten.size() & 0xFF));
  p.push_back((uint8_t) (zeiten.size() >> 8));
  for (int32_t z : zeiten) varint(p, z);
  if (p.size() + HMAC_LEN > MAX_PAKET) {
    ESP_LOGW(TAG, "Signal zu lang fuer ein Paket (%u Byte)", (unsigned) p.size());
    return false;
  }
  uint8_t eigene[6], h[8];
  esp_read_mac(eigene, ESP_MAC_WIFI_STA);
  hmac_(eigene, p.data(), p.size(), h);
  p.insert(p.end(), h, h + HMAC_LEN);
  a.fehlschlag = std::move(fehlschlag);
  warte_.push_back(std::move(a));
  return true;
}

void DockDirekt::funk_zurueck_() {
  if (!funk_geliehen_) return;
  funk_geliehen_ = false;
  // Nur zuruecknehmen, wenn ESPHome das WLAN nicht inzwischen selbst eingeschaltet hat. ESPHome stoppt beim
  // Abschalten mit esp_wifi_stop() und merkt sich das selbst (s_wifi_started) - also wieder genau so stoppen.
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_disabled()) esp_wifi_stop();
  ESP_LOGD(TAG, "Funk wieder aus");
}

void DockDirekt::naechster_versuch_() {
  if (funk_geliehen_ && !kanaele_.empty()) {
    const uint8_t k = kanaele_.front();
    kanaele_.erase(kanaele_.begin());
    esp_wifi_set_channel(k, WIFI_SECOND_CHAN_NONE);
    s_fertig = false;
    gesendet_ms_ = millis();
    const esp_err_t e = esp_now_send(aktuell_.mac, aktuell_.paket.data(), aktuell_.paket.size());
    if (e == ESP_OK) return;
    ESP_LOGW(TAG, "Senden auf Kanal %u: %s", (unsigned) k, esp_err_to_name(e));
    kanaele_.clear();
  }
  // kein Dock erreicht
  ESP_LOGI(TAG, "Dock %02X:%02X:%02X:%02X:%02X:%02X nicht erreicht - Ausweichweg", aktuell_.mac[0], aktuell_.mac[1],
           aktuell_.mac[2], aktuell_.mac[3], aktuell_.mac[4], aktuell_.mac[5]);
  unterwegs_ = false;
  auto f = std::move(aktuell_.fehlschlag);
  aktuell_ = Auftrag{};
  if (f) f();
}

void DockDirekt::loop() {
#ifdef DD_EMPFANG
  // Beim Start ist das WLAN oft noch nicht gestartet: bis ESP-NOW laeuft, alle 2 s neu versuchen
  // (ohne laeuft nichts - die WLAN-Hardware bestaetigt Pakete trotzdem, die Remote merkt es also nicht).
  if (!init_) bereit_();
  for (auto &f : s_fach) {
    if (!f.voll) continue;
    verarbeiten_(f.mac, f.d, f.len);
    f.voll = false;
  }
  if (const uint32_t v = s_verworfen.exchange(0)) ESP_LOGW(TAG, "%u Pakete verworfen (Puffer voll)", (unsigned) v);
  if (const uint32_t v = s_fremd.exchange(0)) ESP_LOGD(TAG, "%u fremde ESP-NOW-Pakete ignoriert", (unsigned) v);
#endif

  auto *w = wifi::global_wifi_component;
  // Kanal des WLANs merken (die Remote braucht ihn, wenn ihr WLAN aus ist)
  if (w != nullptr && w->is_connected() && millis() - kanal_pruef_ms_ > 5000) {
    kanal_pruef_ms_ = millis();
    uint8_t k = 0;
    wifi_second_chan_t s;
    if (esp_wifi_get_channel(&k, &s) == ESP_OK && k != 0 && k != kanal_) {
      ESP_LOGD(TAG, "WLAN-Kanal %u gemerkt", (unsigned) k);
      kanal_ = k;
      kanal_pref_.save(&kanal_);
    }
  }

  if (unterwegs_) {
    if (s_fertig) {
      if (s_ok) {
        n_gesendet_++;
        uint8_t k = 0;
        wifi_second_chan_t sk;
        esp_wifi_get_channel(&k, &sk);
        ESP_LOGD(TAG, "Dock hat es (%u Byte, Kanal %u%s, %u ms)", (unsigned) aktuell_.paket.size(), (unsigned) k,
                 funk_geliehen_ ? ", WLAN war aus" : "", (unsigned) (millis() - gesendet_ms_));
        if (funk_geliehen_) {   // diesen Kanal fuer das naechste Mal
          uint8_t k = 0;
          wifi_second_chan_t s;
          if (esp_wifi_get_channel(&k, &s) == ESP_OK && k != kanal_ && k != 0) { kanal_ = k; kanal_pref_.save(&kanal_); }
        }
        unterwegs_ = false;
        aktuell_ = Auftrag{};
      } else {
        naechster_versuch_();
      }
    } else if (millis() - gesendet_ms_ > ANTWORT_MS) {
      naechster_versuch_();
    }
    if (unterwegs_) return;
  }

  if (warte_.empty()) {
    funk_zurueck_();
    return;
  }
  if (w != nullptr && w->is_disabled() && !funk_geliehen_) {
    // ESPHome hat das WLAN mit esp_wifi_stop() abgeschaltet (Modus bleibt STA): ohne Anmeldung wieder starten
    wifi_mode_t m = WIFI_MODE_NULL;
    esp_wifi_get_mode(&m);
    if (m == WIFI_MODE_NULL) esp_wifi_set_mode(WIFI_MODE_STA);
    const esp_err_t e = esp_wifi_start();
    if (e != ESP_OK) ESP_LOGW(TAG, "Funk starten: %s", esp_err_to_name(e));
    funk_geliehen_ = true;
    funk_ab_ms_ = millis() + 30;   // STA-Start laeuft asynchron
    ESP_LOGD(TAG, "Funk fuer ESP-NOW eingeschaltet (WLAN ist aus)");
    return;
  }
  if (funk_geliehen_ && (int32_t) (millis() - funk_ab_ms_) < 0) return;
  aktuell_ = std::move(warte_.front());
  warte_.erase(warte_.begin());
  kanaele_.clear();
  if (funk_geliehen_) {
    // gemerkter Kanal zuerst, dann alle anderen
    kanaele_.push_back(kanal_);
    for (uint8_t k = 1; k <= 13; k++) if (k != kanal_) kanaele_.push_back(k);
  } else if (w == nullptr || !w->is_connected()) {
    // WLAN hat inzwischen angefangen sich zu verbinden: ausweichen
    unterwegs_ = true;
    naechster_versuch_();
    return;
  }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, aktuell_.mac, 6);
  peer.channel = 0;   // aktueller Kanal
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(aktuell_.mac)) esp_now_add_peer(&peer);
  unterwegs_ = true;
  if (!kanaele_.empty()) { naechster_versuch_(); return; }
  s_fertig = false;
  gesendet_ms_ = millis();
  if (esp_now_send(aktuell_.mac, aktuell_.paket.data(), aktuell_.paket.size()) != ESP_OK) naechster_versuch_();
}

// ------------------------------------------------------------------ Empfangen (Dock)

void DockDirekt::verarbeiten_(const uint8_t mac[6], const uint8_t *d, size_t n) {
#ifdef DD_EMPFANG
  char von[18];
  snprintf(von, sizeof(von), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (d[2] != VER) { ESP_LOGW(TAG, "%s: Version %u unbekannt", von, d[2]); return; }
  uint8_t h[8];
  hmac_(mac, d, n - HMAC_LEN, h);
  uint8_t diff = 0;
  for (size_t i = 0; i < HMAC_LEN; i++) diff |= h[i] ^ d[n - HMAC_LEN + i];
  if (diff) { ESP_LOGW(TAG, "%s: Pruefsumme falsch (anderer Schluessel?) - verworfen", von); return; }
  const uint8_t typ = d[3];
  const uint32_t sitzung = u32_lesen(d + 4), nr = u32_lesen(d + 8), param = u32_lesen(d + 12);
  const uint16_t anzahl = d[16] | (d[17] << 8);
  Absender *a = nullptr;
  for (auto &x : absender_) if (memcmp(x.mac, mac, 6) == 0) a = &x;
  // Nur steigende Nummern (sie zaehlen ueber Neustarts der Remote weiter); nach einem Neustart des Docks gilt die erste
  if (a != nullptr && nr <= a->nr) { ESP_LOGD(TAG, "%s: Nr. %u schon gehabt (zuletzt %u)", von, (unsigned) nr, (unsigned) a->nr); return; }
  if (a == nullptr) {
    if (absender_.size() >= 8) absender_.erase(absender_.begin());
    absender_.push_back(Absender{});
    a = &absender_.back();
    memcpy(a->mac, mac, 6);
  }
  a->sitzung = sitzung;
  a->nr = nr;

  std::vector<int32_t> t;
  t.reserve(anzahl);
  size_t i = KOPF;
  const size_t ende = n - HMAC_LEN;
  while (t.size() < anzahl && i < ende) {
    uint32_t z = 0;
    int s = 0;
    while (i < ende && s < 35) {
      const uint8_t b = d[i++];
      z |= (uint32_t) (b & 0x7F) << s;
      s += 7;
      if (!(b & 0x80)) break;
    }
    const int32_t v = (int32_t) ((z >> 1) ^ (~(z & 1) + 1));
    if (v != 0) t.push_back(v);
  }
  if (t.size() != anzahl || i != ende) { ESP_LOGW(TAG, "%s: Paket kaputt", von); return; }

  remote_transmitter::RemoteTransmitterComponent *tx = typ == IR ? ir_ : typ == RF ? rf_ : nullptr;
  if (tx == nullptr) { ESP_LOGW(TAG, "%s: Typ %u hier nicht moeglich", von, typ); return; }
  n_empfangen_++;
  ESP_LOGI(TAG, "Direkt von %s: %s, %u Zeiten", von, typ == IR ? "Infrarot" : "433 MHz", (unsigned) t.size());
  for (auto &f : senden_cbs_) f(typ);
  auto call = tx->transmit();
  auto *data = call.get_data();
  data->reserve(t.size());
  if (typ == IR) data->set_carrier_frequency(param ? param : 38000);
  for (int32_t v : t) { if (v > 0) data->mark(v); else data->space(-v); }
  if (typ == RF) {
    call.set_send_times(param < 1 ? 5 : (param > 20 ? 20 : param));
    call.set_send_wait(10000);
  }
  call.perform();
#endif
}

}  // namespace dock_direkt
}  // namespace esphome
