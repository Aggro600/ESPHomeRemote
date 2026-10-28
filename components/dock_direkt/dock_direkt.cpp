#include "dock_direkt.h"
#include <array>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"

#include <esp_idf_version.h>
#include <esp_attr.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <freertos/task.h>

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
static const uint16_t NETZ_PORT = 6062;     // Heimnetz-Weg (UDP), s. ip_setzen
static const uint32_t NETZ_HALTEN_MS = 300000;   // so lange nach einem Funk-Fehlschlag gleich das Heimnetz nehmen
static const uint32_t ANTWORT_MS = 150;   // ohne Sende-Rueckmeldung: als Fehlschlag werten

// Rueckmeldungen aus dem WLAN-Task
// Dock, das zuletzt bestaetigt hat - ueber den Tiefschlaf hinweg (2026-10-23): sonst funkte die Remote nach jedem
// Aufwachen reihum auch ausgeschaltete Docks an. Antwortet es nicht mehr, wird es vergessen (naechster_versuch_).
static RTC_DATA_ATTR uint8_t s_rtc_ok_mac[6];
static RTC_DATA_ATTR uint32_t s_rtc_ok_magic;
static const uint32_t RTC_OK_MAGIC = 0x4F4B444Bu;
static std::atomic<bool> s_fertig{false};
static std::atomic<bool> s_ok{false};
// Wer gerade sendet (die Sende-Rueckmeldung oben gibt es nur einmal): 0 = frei, 1 = loop(), 2 = HTTP-Task (2026-10-20)
static std::atomic<int> s_besitzer{0};
static bool besitz_nehmen(int wer) { int frei = 0; return s_besitzer.compare_exchange_strong(frei, wer); }
static void besitz_abgeben(int wer) { int ich = wer; s_besitzer.compare_exchange_strong(ich, 0); }

// HTTP-Pakete (typ 6-9) gehen nicht durch loop(), sondern in diese Schlange des HTTP-Tasks
struct HttpRx { uint8_t mac[6]; std::vector<uint8_t> d; };
static std::mutex s_http_mtx;
static std::deque<HttpRx> s_http_rx;
#ifdef DD_EMPFANG
static const size_t HTTP_RX_MAX = 8;    // Dock (ESP32-C3, wenig RAM): nur Anfragen und Quittungen kommen herein
#else
static const size_t HTTP_RX_MAX = 16;   // Remote: Antwortpakete (PSRAM)
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void gesendet_cb(const esp_now_send_info_t *, esp_now_send_status_t st) {
#else
static void gesendet_cb(const uint8_t *, esp_now_send_status_t st) {
#endif
  s_ok = st == ESP_NOW_SEND_SUCCESS;
  s_fertig = true;
}

// Empfangspuffer: der WLAN-Task schreibt, loop() liest. Dock: 3 Plaetze (Tastendruecke kommen nicht schneller),
// Remote: 1 (sie empfaengt nur die Antwort auf ihre Zustands-Anfrage; spart internen RAM).
#ifdef DD_EMPFANG
static const int FAECHER = 3;
#else
static const int FAECHER = 1;
#endif
struct Fach {
  std::atomic<bool> voll{false};
  uint8_t mac[6];
  uint16_t len;
  uint8_t d[MAX_PAKET];
};
static Fach s_fach[FAECHER];
static std::atomic<uint32_t> s_verworfen{0}, s_fremd{0};

static void empfangen_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int) (KOPF + HMAC_LEN) || len > (int) MAX_PAKET || data[0] != 'O' || data[1] != 'R') { s_fremd++; return; }
  if (data[3] >= HTTP_AUF && data[3] <= HTTP_QUITT) {
    std::lock_guard<std::mutex> l(s_http_mtx);
    if (s_http_rx.size() >= HTTP_RX_MAX) { s_verworfen++; return; }
    HttpRx r;
    memcpy(r.mac, info->src_addr, 6);
    r.d.assign(data, data + len);
    s_http_rx.push_back(std::move(r));
    return;
  }
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
#ifdef DD_HID
  if (hid_ != nullptr)
    hid_->an_remote = [this](uint8_t p, uint8_t art, const std::vector<uint8_t> &d) { this->an_remote_senden_(p, art, d); };
#endif
  if (s_rtc_ok_magic == RTC_OK_MAGIC) { memcpy(ok_mac_, s_rtc_ok_mac, 6); ok_gueltig_ = true; }   // vom Tiefschlaf
  // Auch ueber Neustart/Update hinweg (2026-10-25): sonst probierte die Remote danach erst die ausgeschalteten Docks
  ok_pref_ = global_preferences->make_preference<std::array<uint8_t, 6>>(0x44445204u);
  if (!ok_gueltig_) {
    std::array<uint8_t, 6> m{};
    if (ok_pref_.load(&m) && (m[0] | m[1] | m[2] | m[3] | m[4] | m[5]) != 0) { memcpy(ok_mac_, m.data(), 6); ok_gueltig_ = true; }
  }
  kanal_pref_ = global_preferences->make_preference<uint8_t>(0x44445201u);
  if (!kanal_pref_.load(&kanal_)) kanal_ = 0;
  dock_kanal_pref_ = global_preferences->make_preference<uint8_t>(0x44445203u);
  if (!dock_kanal_pref_.load(&dock_kanal_)) dock_kanal_ = 0;
  // Laufende Nummer (beide Seiten senden): ab dem vorausgespeicherten Stand weiter, naechsten Block gleich sichern
  nr_pref_ = global_preferences->make_preference<uint32_t>(0x44445202u);
  if (!nr_pref_.load(&nr_)) nr_ = 0;
  nr_bis_ = nr_ + 1000;
  nr_pref_.save(&nr_bis_);
  global_preferences->sync();   // sofort schreiben: nach einem Absturz duerfen keine Nummern doppelt vorkommen
  {   // AES-Schluessel fuer HTTP ueber das Dock: SHA-256("dock-http" + Schluessel), erste 16 Byte
    uint8_t h[32];
    mbedtls_md_context_t c;
    mbedtls_md_init(&c);
    mbedtls_md_setup(&c, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&c);
    mbedtls_md_update(&c, (const unsigned char *) "dock-http", 9);
    mbedtls_md_update(&c, (const unsigned char *) schluessel_.data(), schluessel_.size());
    mbedtls_md_finish(&c, h);
    mbedtls_md_free(&c);
    memcpy(http_key_, h, 16);
  }
  bereit_();
#ifdef DD_HA
  if (!ha_http_.empty()) {
    xTaskCreate([](void *a) { static_cast<DockDirekt *>(a)->http_dock_task_(); }, "dock_http", 6144, this, 5, nullptr);
    ESP_LOGI(TAG, "HTTP ueber das Dock: Remotes holen Bilder/Konfiguration ueber %s", ha_http_.c_str());
  }
#endif
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
  if (esp_wifi_get_mode(&m) != ESP_OK) {   // WLAN-Treiber noch nicht gestartet
    // Remote, Weck-Taste (2026-10-23): das WLAN startet erst nach dem fruehen Versand (on_boot 1100/247). Dann den
    // Treiber selbst hochfahren und den Funk "leihen" - ESPHome bindet ihn beim spaeteren enable() an (wifi_lazy_init_).
    auto *w = wifi::global_wifi_component;
    if (w == nullptr || !w->is_disabled()) return false;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_wifi_start() != ESP_OK) return false;
    funk_geliehen_ = true;
    funk_ab_ms_ = millis() + 30;
    ESP_LOGI(TAG, "Funk fuer die Weck-Taste selbst gestartet (WLAN folgt danach)");
  }
  esp_err_t e = esp_now_init();
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "ESP-NOW nicht bereit: %s", esp_err_to_name(e));
    return false;
  }
  esp_now_register_send_cb(gesendet_cb);
  esp_now_register_recv_cb(empfangen_cb);
  uint32_t v = 0;
  esp_now_get_version(&v);
  ESP_LOGI(TAG, "ESP-NOW bereit (Version %u)", (unsigned) v);
  init_ = true;
  return true;
}

// ------------------------------------------------------------------ Senden (Remote)

bool DockDirekt::senden(const uint8_t mac[6], Typ typ, uint32_t param, const std::vector<int32_t> &zeiten,
                        std::function<void()> &&fehlschlag) {
  if (zeiten.empty() || zeiten.size() > 0xFFFF) return false;
  std::vector<uint8_t> inhalt;
  inhalt.reserve(zeiten.size() * 2);
  for (int32_t z : zeiten) varint(inhalt, z);
  return einreihen_(mac, typ, param, (uint16_t) zeiten.size(), inhalt, std::move(fehlschlag));
}

bool DockDirekt::senden_ha(const uint8_t mac[6], const std::string &dienst,
                           const std::vector<std::pair<std::string, std::string>> &daten, std::function<void()> &&fehlschlag) {
  std::vector<uint8_t> inhalt(dienst.begin(), dienst.end());
  inhalt.push_back(0);
  for (auto &kv : daten) {
    inhalt.insert(inhalt.end(), kv.first.begin(), kv.first.end());
    inhalt.push_back(0);
    inhalt.insert(inhalt.end(), kv.second.begin(), kv.second.end());
    inhalt.push_back(0);
  }
  if (inhalt.size() > 0xFFFF) return false;
  return einreihen_(mac, HA, 0, (uint16_t) inhalt.size(), inhalt, std::move(fehlschlag));
}

bool DockDirekt::senden_sprache(const uint8_t mac[6], uint8_t platz, const std::vector<uint8_t> &inhalt,
                               std::function<void()> &&fehlschlag) {
  if (inhalt.empty() || inhalt.size() + KOPF + HMAC_LEN > MAX_PAKET) return false;
  antwort_abwarten(400);   // Funk zwischen den Paketen offen halten (alle ~130 ms eins)
  return einreihen_(mac, SPRACHE, platz, (uint16_t) inhalt.size(), inhalt, std::move(fehlschlag));
}

// Dock: Wunsch des Fernsehers an die Remote, die zuletzt an diesem Platz bedient hat (2026-10-24). Ihr Funk ist nach
// einem Tastendruck noch kurz offen (Remote: antwort_abwarten) - bis zu 5 Versuche im Abstand von 250 ms.
void DockDirekt::an_remote_senden_(uint8_t platz, uint8_t art, const std::vector<uint8_t> &daten) {
  if (platz >= 4 || !platz_remote_[platz].da || millis() - platz_remote_[platz].ms > 120000) {
    ESP_LOGW(TAG, "Fernseher an Platz %u will das Mikrofon - keine Remote hat dort zuletzt bedient", (unsigned) platz + 1);
    return;
  }
  AnRemote a;
  memcpy(a.mac, platz_remote_[platz].mac, 6);
  a.inhalt = {art, (uint8_t) (daten.size() & 0xFF), (uint8_t) (daten.size() >> 8)};
  a.inhalt.insert(a.inhalt.end(), daten.begin(), daten.end());
  a.platz = platz;
  a.rest = 5;
  a.ab_ms = millis();
  a.nr = ++an_remote_nr_;
  a.unterwegs = false;
  if (art == 3) for (auto &x : an_remote_) if (x.platz == platz) x.rest = 0;   // Schliessen ersetzt ein offenes Oeffnen
  an_remote_.push_back(std::move(a));
}

bool DockDirekt::zustand_anfragen(const uint8_t mac[6], const std::string &name, std::function<void()> &&fehlschlag) {
  std::vector<uint8_t> inhalt(name.begin(), name.end());
  return einreihen_(mac, ZUSTAND_FRAGE, 0, (uint16_t) inhalt.size(), inhalt, std::move(fehlschlag));
}

bool DockDirekt::senden_hid(const uint8_t mac[6], uint8_t platz, bool antwort, const std::vector<uint8_t> &ereignisse,
                            std::function<void()> &&fehlschlag) {
  if (ereignisse.empty() || ereignisse.size() % 6 != 0) return false;
  return einreihen_(mac, HID, platz | (antwort ? 0x100u : 0u), (uint16_t) (ereignisse.size() / 6), ereignisse,
                    std::move(fehlschlag));
}

void DockDirekt::zustand_setzen(const std::string &remote, const std::string &text) {
  if (text.size() + KOPF + HMAC_LEN > MAX_PAKET) { ESP_LOGW(TAG, "Stand fuer %s zu gross (%u Byte)", remote.c_str(), (unsigned) text.size()); return; }
  if (zustaende_.size() >= 8 && zustaende_.find(remote) == zustaende_.end()) zustaende_.erase(zustaende_.begin());
  zustaende_[remote] = text;
  ESP_LOGD(TAG, "Stand fuer %s: %u Byte", remote.c_str(), (unsigned) text.size());
}

bool DockDirekt::einreihen_(const uint8_t mac[6], Typ typ, uint32_t param, uint16_t anzahl, const std::vector<uint8_t> &inhalt,
                            std::function<void()> &&fehlschlag) {
  if (warte_.size() >= 6 || !bereit_()) return false;
  auto *w = wifi::global_wifi_component;
  // WLAN verbindet sich gerade (sucht Kanaele): nicht dazwischenfunken
  if (funk_belegt()) {
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
  p.reserve(KOPF + inhalt.size() + HMAC_LEN);
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
  p.push_back((uint8_t) (anzahl & 0xFF));
  p.push_back((uint8_t) (anzahl >> 8));
  p.insert(p.end(), inhalt.begin(), inhalt.end());
  if (p.size() + HMAC_LEN > MAX_PAKET) {
    ESP_LOGW(TAG, "Inhalt zu lang fuer ein Paket (%u Byte)", (unsigned) p.size());
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

bool DockDirekt::funk_belegt() const {
  auto *w = wifi::global_wifi_component;
  return w == nullptr || !(w->is_connected() || w->is_disabled() || (frueh_ && kanal_ != 0));
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
  // Einzelnes verlorenes Paket (2026-10-27): das Dock teilt seinen Funk mit WLAN und Bluetooth - erst zweimal
  // nachlegen, bevor das Dock als "nicht erreicht" gilt (sonst schaltet ein Paket den Bluetooth-Notweg ein)
  if (!funk_geliehen_ && aktuell_.gesendet && aktuell_.nochmal > 0) {
    aktuell_.nochmal--;
    s_fertig = false;
    gesendet_ms_ = millis();
    if (esp_now_send(aktuell_.mac, aktuell_.paket.data(), aktuell_.paket.size()) == ESP_OK) return;
  }
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
  // Funk ging nicht: WLAN verbunden und IP des Docks bekannt -> Heimnetz (anderer Kanal/Zugangspunkt)
  if (!funk_geliehen_ && (!netz_modus || netz_modus() != 1) && netz_statt_funk_()) {
    if (NetzZiel *nz = netz_ziel_(aktuell_.mac)) nz->bevorzugt_bis = millis() + NETZ_HALTEN_MS;
    if (!netz_gemeldet_) {
      netz_gemeldet_ = true;
      ESP_LOGI(TAG, "Dock %02X:%02X:%02X:%02X:%02X:%02X per Funk nicht erreicht - nehme das Heimnetz (anderer WLAN-Kanal?)",
               aktuell_.mac[0], aktuell_.mac[1], aktuell_.mac[2], aktuell_.mac[3], aktuell_.mac[4], aktuell_.mac[5]);
    }
    return;
  }
  // Das gemerkte Dock bleibt trotzdem vorn (2026-10-25): vergessen hiess, beim naechsten Mal erst die ausgeschalteten
  // Docks durchzuprobieren (je ~1 s). Antwortet ein anderes, wird das gemerkt.
  // kein Dock erreicht
  ESP_LOGI(TAG, "Dock %02X:%02X:%02X:%02X:%02X:%02X nicht erreicht - Ausweichweg", aktuell_.mac[0], aktuell_.mac[1],
           aktuell_.mac[2], aktuell_.mac[3], aktuell_.mac[4], aktuell_.mac[5]);
  unterwegs_ = false;
  besitz_abgeben(1);
  auto f = std::move(aktuell_.fehlschlag);
  aktuell_ = Auftrag{};
  if (f) f();
}

void DockDirekt::loop() {
#ifdef DD_HID
  for (size_t i = 0; i < an_remote_.size();) {
    auto &a = an_remote_[i];
    if (a.unterwegs && warteschlange() == 0) { an_remote_.erase(an_remote_.begin() + i); continue; }   // angekommen
    if (!a.unterwegs && a.rest == 0) {
      ESP_LOGW(TAG, "Wunsch des Fernsehers (Platz %u) kam bei der Remote nicht an", (unsigned) a.platz + 1);
      an_remote_.erase(an_remote_.begin() + i);
      continue;
    }
    if (!a.unterwegs && (int32_t) (millis() - a.ab_ms) >= 0) {
      const uint32_t nr = a.nr;
      std::vector<uint8_t> inhalt = a.inhalt;
      uint8_t mac[6];
      memcpy(mac, a.mac, 6);
      a.rest--;
      a.ab_ms = millis() + 250;
      // Kein Fehlschlag-Rueckruf = angekommen; sonst nach 250 ms nochmal (Funk der Remote evtl. gerade zu)
      if (einreihen_(mac, SPRACHE, a.platz, (uint16_t) inhalt.size(), inhalt, [this, nr]() {
            for (auto &x : an_remote_) if (x.nr == nr) x.unterwegs = false;
          }))
        an_remote_[i].unterwegs = true;
    }
    i++;
  }
#endif
#ifdef DD_EMPFANG
  // Beim Start ist das WLAN oft noch nicht gestartet: bis ESP-NOW laeuft, alle 2 s neu versuchen
  // (ohne laeuft nichts - die WLAN-Hardware bestaetigt Pakete trotzdem, die Remote merkt es also nicht).
  if (!init_) bereit_();
#endif
  for (auto &f : s_fach) {
    if (!f.voll) continue;
#ifdef DD_EMPFANG
    // Kam das Paket per Funk, antworten wir auch per Funk (2026-10-28): nach einer WLAN-Phase der Remote (Flashen,
    // Wartungsfenster, WLAN-Knopf) gingen Staende 5 min lang ans Heimnetz - die Remote war da schon wieder ohne WLAN,
    // UDP merkt das nicht. Folge: keine Popups (Kamera), keine Startseiten-Werte. HTTP lief schon immer per Funk.
    if (NetzZiel *z = netz_ziel_(f.mac)) z->bevorzugt_bis = millis();
#endif
    verarbeiten_(f.mac, f.d, f.len);
    f.voll = false;
  }
  udp_lesen_();
  if (const uint32_t v = s_verworfen.exchange(0)) ESP_LOGW(TAG, "%u Pakete verworfen (Puffer voll)", (unsigned) v);
  if (const uint32_t v = s_fremd.exchange(0)) ESP_LOGD(TAG, "%u fremde ESP-NOW-Pakete ignoriert", (unsigned) v);
#ifdef DD_HA
  if (!ha_warte_.empty()) {
    const bool da = api::global_api_server != nullptr && api::global_api_server->is_connected();
    for (auto it = ha_warte_.begin(); it != ha_warte_.end();) {
      if (da) { this->call_homeassistant_service(it->dienst, it->daten); it = ha_warte_.erase(it); }
      else if (millis() - it->t0 > 30000) { ESP_LOGW(TAG, "HA-Aktion %s verworfen (keine HA-Verbindung)", it->dienst.c_str()); it = ha_warte_.erase(it); }
      else ++it;
    }
  }
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
          if (esp_wifi_get_channel(&k, &s) == ESP_OK && k != dock_kanal_ && k != 0) { dock_kanal_ = k; dock_kanal_pref_.save(&dock_kanal_); }
        }
#ifndef DD_EMPFANG   // nur die Remote (das Dock wechselt laufend zwischen den Remotes)
        if (memcmp(ok_mac_, aktuell_.mac, 6) != 0 || !ok_gueltig_) {   // nur bei Wechsel ins Flash
          std::array<uint8_t, 6> m;
          memcpy(m.data(), aktuell_.mac, 6);
          ok_pref_.save(&m);
        }
#endif
        memcpy(ok_mac_, aktuell_.mac, 6);
        ok_gueltig_ = true;
        memcpy(s_rtc_ok_mac, aktuell_.mac, 6);
        s_rtc_ok_magic = RTC_OK_MAGIC;
        ok_ms_ = millis() | 1;
        // Zustands-Anfrage: Funk kurz offen halten, damit die Antwort des Docks ankommt
        if (aktuell_.paket.size() > 3 && aktuell_.paket[3] == ZUSTAND_FRAGE) antwort_bis_ms_ = millis() + 400;
        // Bluetooth ueber das Dock: ist der Platz dort nicht verbunden, antwortet das Dock (typ 11) - kurz zuhoeren
        if (aktuell_.paket.size() > 3 && aktuell_.paket[3] == HID) antwort_abwarten(250);
        unterwegs_ = false;
        aktuell_ = Auftrag{};
        besitz_abgeben(1);
      } else {
        naechster_versuch_();
      }
    } else if (millis() - gesendet_ms_ > ANTWORT_MS) {
      naechster_versuch_();
    }
    if (unterwegs_) return;
  }

  // HTTP-Task der Remote braucht den Funk (HTTP ueber das Dock): WLAN verbunden -> sofort; WLAN aus -> leihen
  if (http_funk_wunsch_) {
    if (w != nullptr && w->is_connected()) {
      http_funk_bereit_ = true;
    } else if (w != nullptr && w->is_disabled() && kanal_ != 0) {
      if (!funk_geliehen_) {
        wifi_mode_t m = WIFI_MODE_NULL;
        esp_wifi_get_mode(&m);
        if (m == WIFI_MODE_NULL) esp_wifi_set_mode(WIFI_MODE_STA);
        if (esp_wifi_start() == ESP_OK) { funk_geliehen_ = true; funk_ab_ms_ = millis() + 30; }
      } else if (!http_funk_bereit_ && (int32_t) (millis() - funk_ab_ms_) >= 0) {
        esp_wifi_set_channel(dock_kanal_ != 0 ? dock_kanal_ : kanal_, WIFI_SECOND_CHAN_NONE);   // Kanal des Docks
        http_funk_bereit_ = true;
      }
    }
  } else {
    http_funk_bereit_ = false;
  }
  if (s_besitzer == 2) return;   // HTTP-Task sendet gerade - eigene Auftraege warten

  if (warte_.empty()) {
    if ((int32_t) (millis() - antwort_bis_ms_) >= 0 && !http_funk_wunsch_) funk_zurueck_();
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
  if (frueh_ && w != nullptr && !w->is_connected() && !w->is_disabled() && !wlan_bereit_frueh_()) return;
  if (!besitz_nehmen(1)) return;   // HTTP-Task war schneller
  aktuell_ = std::move(warte_.front());
  warte_.erase(warte_.begin());
  kanaele_.clear();
  if (funk_geliehen_) {
    // Kanal, auf dem das Dock zuletzt geantwortet hat, dann der WLAN-Kanal, dann alle anderen
    if (dock_kanal_ != 0) kanaele_.push_back(dock_kanal_);
    if (kanal_ != 0 && kanal_ != dock_kanal_) kanaele_.push_back(kanal_);
    for (uint8_t k = 1; k <= 13; k++) if (k != kanal_ && k != dock_kanal_) kanaele_.push_back(k);
  } else if (frueh_ && w != nullptr && !w->is_connected()) {
    // Start aus dem Tiefschlaf: WLAN verbindet gerade - auf seinem Kanal senden (Funk laeuft, s. oben)
  } else if (w == nullptr || !w->is_connected()) {
    // WLAN hat inzwischen angefangen sich zu verbinden: ausweichen
    unterwegs_ = true;
    naechster_versuch_();
    return;
  }
  if (!funk_geliehen_ && dock_kanal_ != 0 && (!netz_modus || netz_modus() != 1) && netz_ziel_(aktuell_.mac) != nullptr) {
    // Das Dock antwortete zuletzt auf einem anderen Kanal als dem des WLANs (Dock am Repeater, Remote an der Box,
    // 2026-10-23): Funk kann nicht ankommen - gleich uebers Heimnetz, beim Start erst warten, bis das WLAN steht (~0,1 s)
    uint8_t k = 0;
    wifi_second_chan_t sk;
    if (esp_wifi_get_channel(&k, &sk) == ESP_OK && k != 0 && k != dock_kanal_) {
      if (w != nullptr && w->is_connected()) {
        unterwegs_ = true;
        if (netz_statt_funk_()) return;
        unterwegs_ = false;
      } else if (frueh_) {
        warte_.insert(warte_.begin(), std::move(aktuell_));
        aktuell_ = Auftrag{};
        besitz_abgeben(1);
        return;
      }
    }
  }
  if (!funk_geliehen_ && !frueh_) {   // Heimnetz bevorzugt (Funk ging zuletzt nicht, oder Einstellung "Heimnetz zuerst")
    const int nm = netz_modus ? netz_modus() : 0;
    NetzZiel *nz = netz_ziel_(aktuell_.mac);
    if (nz != nullptr && nm != 1 && (nm == 2 || (int32_t) (nz->bevorzugt_bis - millis()) > 0)) {
      unterwegs_ = true;
      if (netz_statt_funk_()) return;
      unterwegs_ = false;
    }
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
  aktuell_.gesendet = true;
  if (esp_now_send(aktuell_.mac, aktuell_.paket.data(), aktuell_.paket.size()) != ESP_OK) naechster_versuch_();
}

// ------------------------------------------------------------------ Heimnetz (UDP, 2026-10-23)

void DockDirekt::ip_setzen(const uint8_t mac[6], const std::string &ip) {
  struct in_addr a;
  if (ip.empty() || inet_aton(ip.c_str(), &a) == 0) return;
  NetzZiel *z = netz_ziel_(mac);
  if (z == nullptr) {
    if (netz_.size() >= 8) netz_.erase(netz_.begin());
    netz_.push_back(NetzZiel{});
    z = &netz_.back();
    memcpy(z->mac, mac, 6);
    z->bevorzugt_bis = millis();
  }
  z->ip = a.s_addr;
}

DockDirekt::NetzZiel *DockDirekt::netz_ziel_(const uint8_t mac[6]) {
  for (auto &z : netz_) if (memcmp(z.mac, mac, 6) == 0) return &z;
  return nullptr;
}

bool DockDirekt::udp_bereit_() {
  if (udp_ >= 0) return true;
  auto *w = wifi::global_wifi_component;
  if (w == nullptr || !w->is_connected()) return false;
  const int s = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return false;
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(NETZ_PORT);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(s, (struct sockaddr *) &a, sizeof(a)) != 0) { lwip_close(s); return false; }
  lwip_fcntl(s, F_SETFL, lwip_fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  udp_ = s;
  ESP_LOGD(TAG, "Heimnetz-Weg bereit (UDP %u)", (unsigned) NETZ_PORT);
  return true;
}

bool DockDirekt::udp_senden_(const NetzZiel &z, const std::vector<uint8_t> &p) {
  if (z.ip == 0 || !udp_bereit_()) return false;
  static std::vector<uint8_t> buf;
  buf.assign({'O', 'N'});
  uint8_t eigene[6];
  esp_read_mac(eigene, ESP_MAC_WIFI_STA);
  buf.insert(buf.end(), eigene, eigene + 6);
  buf.insert(buf.end(), p.begin(), p.end());
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(NETZ_PORT);
  a.sin_addr.s_addr = z.ip;
  return lwip_sendto(udp_, buf.data(), buf.size(), 0, (struct sockaddr *) &a, sizeof(a)) == (int) buf.size();
}

bool DockDirekt::netz_statt_funk_() {
  auto *w = wifi::global_wifi_component;
  if (w == nullptr || !w->is_connected()) return false;
  NetzZiel *nz = netz_ziel_(aktuell_.mac);
  if (nz == nullptr || !udp_senden_(*nz, aktuell_.paket)) return false;
  n_netz_++;
  gesendet_ms_ = millis();
  kanaele_.clear();
  s_ok = true;   // wie eine Funk-Bestaetigung: loop() schliesst den Auftrag ab
  s_fertig = true;
  return true;
}

bool DockDirekt::echt_(const uint8_t mac[6], const uint8_t *d, size_t n) const {
  if (n < KOPF + HMAC_LEN || d[0] != 'O' || d[1] != 'R') return false;
  uint8_t h[8];
  hmac_(mac, d, n - HMAC_LEN, h);
  uint8_t diff = 0;
  for (size_t i = 0; i < HMAC_LEN; i++) diff |= h[i] ^ d[n - HMAC_LEN + i];
  return diff == 0;
}

void DockDirekt::udp_lesen_() {
#ifndef DD_EMPFANG
  if (netz_.empty()) return;   // Remote ohne bekannte Dock-IPs: kein Socket
#endif
  if (!udp_bereit_()) return;
  static uint8_t buf[MAX_PAKET + 8];
  for (int i = 0; i < 4; i++) {
    struct sockaddr_in von = {};
    socklen_t vl = sizeof(von);
    const int n = lwip_recvfrom(udp_, buf, sizeof(buf), 0, (struct sockaddr *) &von, &vl);
    if (n <= 0) return;
    if (n < 8 + (int) (KOPF + HMAC_LEN) || buf[0] != 'O' || buf[1] != 'N') continue;
    const uint8_t *mac = buf + 2, *d = buf + 8;
    const size_t len = n - 8;
    if (!echt_(mac, d, len)) { ESP_LOGW(TAG, "Heimnetz: Paket mit falscher Pruefsumme verworfen"); continue; }
    if (d[3] >= HTTP_AUF && d[3] <= HTTP_QUITT) continue;   // HTTP ueber das Dock bleibt beim Funk
    // Antworten an diesen Absender ebenfalls uebers Heimnetz
    NetzZiel *z = netz_ziel_(mac);
    if (z == nullptr) {
      if (netz_.size() >= 8) netz_.erase(netz_.begin());
      netz_.push_back(NetzZiel{});
      z = &netz_.back();
      memcpy(z->mac, mac, 6);
    }
    z->ip = von.sin_addr.s_addr;
    z->bevorzugt_bis = millis() + NETZ_HALTEN_MS;
    verarbeiten_(mac, d, len);
  }
}

bool DockDirekt::wlan_bereit_frueh_() {
  // Der Funk laeuft, sobald der WLAN-Treiber einen Kanal meldet; verbindet er auf einem anderen als dem gemerkten
  // (Router hat gewechselt), trotzdem probieren - kommt keine Bestaetigung, greift der Ausweichweg.
  uint8_t k = 0;
  wifi_second_chan_t s;
  return esp_wifi_get_channel(&k, &s) == ESP_OK && k != 0;
}

void DockDirekt::pumpen(uint32_t max_ms) {
  const uint32_t t0 = millis();
  while ((!warte_.empty() || unterwegs_) && millis() - t0 < max_ms) {
    this->loop();
    delay(1);
  }
  ESP_LOGI(TAG, "Frueher Versand: %s nach %u ms", (warte_.empty() && !unterwegs_) ? "fertig" : "noch unterwegs",
           (unsigned) (millis() - t0));
}

// ------------------------------------------------------------------ Empfangen (Dock)

void DockDirekt::verarbeiten_(const uint8_t mac[6], const uint8_t *d, size_t n) {
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

  const size_t ende = n - HMAC_LEN;
  if (typ == ZUSTAND_FRAGE || typ == ZUSTAND) {
    if (KOPF + anzahl != ende) { ESP_LOGW(TAG, "%s: Zustands-Paket kaputt", von); return; }
    std::string text((const char *) d + KOPF, anzahl);
#ifdef DD_EMPFANG
    if (typ == ZUSTAND_FRAGE) {   // Dock: Remote fragt nach ihrem Stand (Inhalt = ihr Geraetename)
#ifdef DD_HID
      if (hid_ != nullptr) hid_->weckruf(text);   // Remote ist gerade aufgewacht -> kurz schnell suchen (Scan-Stufen)
#endif
      auto it = zustaende_.find(text);
      if (it == zustaende_.end()) { ESP_LOGI(TAG, "%s (%s) fragt - noch kein Stand", von, text.c_str()); return; }
      std::vector<uint8_t> inhalt(it->second.begin(), it->second.end());
      if (einreihen_(mac, ZUSTAND, 0, (uint16_t) inhalt.size(), inhalt, {}))
        ESP_LOGI(TAG, "%s (%s): Stand geschickt (%u Byte)", von, text.c_str(), (unsigned) inhalt.size());
    }
#else
    if (typ == ZUSTAND) {   // Remote: Stand vom Dock
      ESP_LOGI(TAG, "Stand vom Dock %s (%u Byte)", von, (unsigned) text.size());
      antwort_bis_ms_ = 0;
      for (auto &f : zustand_cbs_) f(text);
    }
#endif
    return;
  }
  if (typ == HID || typ == HID_ANTWORT) {
    if (typ == HID && KOPF + (size_t) anzahl * 6 != ende) { ESP_LOGW(TAG, "%s: Eingabe-Paket kaputt", von); return; }
    if (typ == HID_ANTWORT && KOPF + 2 != ende) { ESP_LOGW(TAG, "%s: Antwort kaputt", von); return; }
#ifdef DD_HID
    if (typ == HID) {
      const uint8_t platz = param & 0xFF;
      if (platz < 4) { memcpy(platz_remote_[platz].mac, mac, 6); platz_remote_[platz].ms = millis(); platz_remote_[platz].da = true; }
      bool ok = hid_ != nullptr;
      for (uint16_t k = 0; k < anzahl && ok; k++) {
        const uint8_t *q = d + KOPF + k * 6;
        dock_hid::Ereignis e{q[0], q[1], {q[2], q[3], q[4], q[5]}};
        ok = hid_->senden(platz, e);
      }
      n_empfangen_++;
      if (!(anzahl == 1 && d[KOPF] == dock_hid::MAUS)) {
        ESP_LOGI(TAG, "Direkt von %s: Bluetooth an Platz %u, %u Ereignis(se)%s", von, (unsigned) platz + 1,
                 (unsigned) anzahl, ok ? "" : " - Platz nicht verbunden");
        // Bluetooth-Tasten blinken wie Infrarot und HA-Befehle (2026-10-25, vorher blinkte nur die Lautstaerke)
        if (ok) for (auto &f : senden_cbs_) f(typ);
      }
      if (!ok || (param & 0x100)) {
        std::vector<uint8_t> inhalt{platz, (uint8_t) (ok ? 1 : 0)};
        einreihen_(mac, HID_ANTWORT, hid_ != nullptr ? hid_->maske() : 0, 2, inhalt, {});
      }
      return;
    }
#endif
    if (typ == HID_ANTWORT) {
      antwort_bis_ms_ = 0;
      for (auto &f : hid_cbs_) f(mac, param & 0xFF, d[KOPF], d[KOPF + 1] != 0);
      return;
    }
    ESP_LOGW(TAG, "%s: Bluetooth ueber das Dock ist hier nicht eingeschaltet (dock_direkt: hid)", von);
    return;
  }
  if (typ == SPRACHE) {
    if (KOPF + anzahl != ende) { ESP_LOGW(TAG, "%s: Sprach-Paket kaputt", von); return; }
    const uint8_t platz = param & 0xFF;
    const uint8_t *q = d + KOPF, *qe = d + ende;
#ifdef DD_HID
    if (platz < 4) { memcpy(platz_remote_[platz].mac, mac, 6); platz_remote_[platz].ms = millis(); platz_remote_[platz].da = true; }
    bool ok = hid_ != nullptr;
    while (ok && q + 3 <= qe) {
      const uint8_t art = q[0];
      const uint16_t l = (uint16_t) (q[1] | (q[2] << 8));
      if (q + 3 + l > qe) { ESP_LOGW(TAG, "%s: Sprach-Abschnitt zu lang", von); break; }
      ok = hid_->sprache(platz, art, q + 3, l);
      q += 3 + l;
    }
    if (!ok) ESP_LOGW(TAG, "Sprache von %s fuer Platz %u: nicht weitergegeben", von, (unsigned) platz + 1);
#else
    // Remote: Wunsch des Fernsehers ueber das Dock (Mikrofon oeffnen/schliessen)
    while (q + 3 <= qe) {
      const uint8_t art = q[0];
      const uint16_t l = (uint16_t) (q[1] | (q[2] << 8));
      if (q + 3 + l > qe) break;
      ESP_LOGI(TAG, "Fernseher (Platz %u) ueber Dock %s: Mikrofon %s", (unsigned) platz + 1, von, art == 2 ? "oeffnen" : "schliessen");
      for (auto &f : sprache_cbs_) f(platz, art, q + 3, l);
      q += 3 + l;
    }
#endif
    return;
  }
  if (typ == HA) {
#ifdef DD_HA
    if (KOPF + anzahl != ende || anzahl == 0 || d[ende - 1] != 0) { ESP_LOGW(TAG, "%s: HA-Paket kaputt", von); return; }
    std::vector<std::string> teile;
    const char *q = (const char *) d + KOPF, *qe = (const char *) d + ende;
    while (q < qe) { const size_t l = strnlen(q, qe - q); teile.emplace_back(q, l); q += l + 1; }
    if (teile.empty() || teile[0].find('.') == std::string::npos || teile.size() % 2 != 1) { ESP_LOGW(TAG, "%s: HA-Aktion ungueltig", von); return; }
    HaAuftrag job{teile[0], {}, millis()};
    for (size_t k = 1; k + 1 < teile.size(); k += 2) job.daten[teile[k]] = teile[k + 1];
    // Werte der Remotes (esphomeremote_konfig.remote_status): je Remote hoechstens alle 20 s weiterreichen, der letzte
    // Stand vor dem Tiefschlaf immer (2026-10-23: zwei Remotes mit 5-s-Takt brachten das Dock an den Speicherrand)
    if (job.dienst == "esphomeremote_konfig.remote_status" && job.daten["t"] != "Tiefschlaf") {
      static std::map<std::string, uint32_t> zuletzt;
      const std::string &g = job.daten["geraet"];
      auto it = zuletzt.find(g);
      if (it != zuletzt.end() && millis() - it->second < 20000) { ESP_LOGD(TAG, "%s: Status zu oft - verworfen", von); return; }
      if (zuletzt.size() >= 8) zuletzt.clear();
      zuletzt[g] = millis();
    }
    n_empfangen_++;
    ESP_LOGI(TAG, "Direkt von %s: HA-Aktion %s (%u Werte)", von, job.dienst.c_str(), (unsigned) job.daten.size());
    // Blinken nur fuer Tasten, nicht fuer Hintergrundmeldungen der Remote (Status, Protokoll, Einstellungen - 2026-10-25)
    if (job.dienst.rfind("esphomeremote_konfig.", 0) != 0)
      for (auto &f : senden_cbs_) f(typ);
    ha_ausfuehren_(job);
#else
    ESP_LOGW(TAG, "%s: HA-Aktionen sind hier nicht eingeschaltet (dock_direkt: ha: true)", von);
#endif
    return;
  }
#ifdef DD_EMPFANG
  std::vector<int32_t> t;
  t.reserve(anzahl);
  size_t i = KOPF;
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
#else
  ESP_LOGW(TAG, "%s: Typ %u hier nicht moeglich", von, typ);
#endif
}

#ifdef DD_HA
void DockDirekt::ha_ausfuehren_(HaAuftrag &a) {
  if (api::global_api_server != nullptr && api::global_api_server->is_connected()) {
    this->call_homeassistant_service(a.dienst, a.daten);
    return;
  }
  if (ha_warte_.size() < 8) ha_warte_.push_back(a);
  ESP_LOGI(TAG, "HA-Aktion %s wartet auf die HA-Verbindung", a.dienst.c_str());
}
#endif


// ------------------------------------------------------------------ HTTP ueber das Dock (2026-10-20)

static const size_t HTTP_STUECK = MAX_PAKET - KOPF - HMAC_LEN - 4;   // Nutzbytes je Datenpaket (Versatz davor)
static const size_t HTTP_FENSTER = 4 * HTTP_STUECK;                   // so viel darf unquittiert unterwegs sein

static void u32_setzen(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i)); }

void DockDirekt::aes_ctr_(uint8_t typ, uint32_t sitzung, uint32_t nr, uint32_t id, uint8_t *d, size_t n) const {
  if (n == 0) return;
  uint8_t nonce[16] = {}, block[16];
  nonce[0] = typ;
  u32_setzen(nonce + 1, sitzung);
  u32_setzen(nonce + 5, nr);
  u32_setzen(nonce + 9, id);
  size_t off = 0;
  mbedtls_aes_context a;
  mbedtls_aes_init(&a);
  mbedtls_aes_setkey_enc(&a, http_key_, 128);
  mbedtls_aes_crypt_ctr(&a, n, &off, nonce, block, d, d);
  mbedtls_aes_free(&a);
}

bool DockDirekt::http_paket_(const uint8_t mac[6], Typ typ, uint32_t id, const uint8_t *d, size_t n, std::vector<uint8_t> &p) {
  if (KOPF + n + HMAC_LEN > MAX_PAKET) return false;
  const uint32_t nr = ++http_nr_;
  p.clear();
  p.reserve(KOPF + n + HMAC_LEN);
  p.push_back('O');
  p.push_back('R');
  p.push_back(VER);
  p.push_back((uint8_t) typ);
  u32(p, sitzung_);
  u32(p, nr);
  u32(p, id);
  p.push_back((uint8_t) (n & 0xFF));
  p.push_back((uint8_t) (n >> 8));
  const size_t a = p.size();
  p.insert(p.end(), d, d + n);
  aes_ctr_((uint8_t) typ, sitzung_, nr, id, p.data() + a, n);
  uint8_t eigene[6], h[8];
  esp_read_mac(eigene, ESP_MAC_WIFI_STA);
  hmac_(eigene, p.data(), p.size(), h);
  p.insert(p.end(), h, h + HMAC_LEN);
  (void) mac;
  return true;
}

bool DockDirekt::http_lesen_(const uint8_t mac[6], const std::vector<uint8_t> &p, uint8_t &typ, uint32_t &id,
                             std::vector<uint8_t> &inhalt) {
  if (p.size() < KOPF + HMAC_LEN || p[2] != VER) return false;
  const size_t ende = p.size() - HMAC_LEN;
  uint8_t h[8], diff = 0;
  hmac_(mac, p.data(), ende, h);
  for (size_t i = 0; i < HMAC_LEN; i++) diff |= h[i] ^ p[ende + i];
  if (diff) return false;
  const uint16_t n = p[16] | (p[17] << 8);
  if (KOPF + n != ende) return false;
  typ = p[3];
  id = u32_lesen(p.data() + 12);
  inhalt.assign(p.begin() + KOPF, p.begin() + ende);
  aes_ctr_(typ, u32_lesen(p.data() + 4), u32_lesen(p.data() + 8), id, inhalt.data(), inhalt.size());
  return true;
}

// Ein Paket senden und auf die Empfangsbestaetigung (MAC-ACK) warten - nur mit Sende-Besitz 2 aufrufen.
bool DockDirekt::roh_senden_(const uint8_t mac[6], const std::vector<uint8_t> &p, int versuche) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
  for (int i = 0; i < versuche; i++) {
    s_fertig = false;
    if (esp_now_send(mac, p.data(), p.size()) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    const uint32_t t0 = millis();
    while (!s_fertig && millis() - t0 < ANTWORT_MS) vTaskDelay(1);
    if (s_fertig && s_ok) return true;
  }
  return false;
}

static bool http_holen(HttpRx &out) {
  std::lock_guard<std::mutex> l(s_http_mtx);
  if (s_http_rx.empty()) return false;
  out = std::move(s_http_rx.front());
  s_http_rx.pop_front();
  return true;
}
static void http_leeren() {
  std::lock_guard<std::mutex> l(s_http_mtx);
  s_http_rx.clear();
}

void DockDirekt::set_http_ziele(const std::vector<std::string> &macs) {
  std::lock_guard<std::mutex> l(s_http_mtx);
  http_ziele_ = macs;
}

// Anfrage vom lokalen Socket lesen: Kopf bis Leerzeile, dann Content-Length Bytes (hoechstens 8 KB)
static bool anfrage_lesen(int c, std::vector<uint8_t> &v) {
  const uint32_t t0 = millis();
  size_t koerper = 0, kopf_ende = 0;
  uint8_t b[512];
  while (millis() - t0 < 2000 && v.size() < 8192) {
    if (kopf_ende != 0 && v.size() >= kopf_ende + koerper) return true;
    fd_set r;
    FD_ZERO(&r);
    FD_SET(c, &r);
    struct timeval tv = {0, 100000};
    if (lwip_select(c + 1, &r, nullptr, nullptr, &tv) <= 0) continue;
    const int k = lwip_recv(c, b, sizeof(b), 0);
    if (k <= 0) return false;
    v.insert(v.end(), b, b + k);
    if (kopf_ende == 0) {
      const std::string t(v.begin(), v.end());
      const size_t e = t.find("\r\n\r\n");
      if (e != std::string::npos) {
        kopf_ende = e + 4;
        std::string kl = t.substr(0, e);
        for (auto &ch : kl) ch = (char) tolower(ch);
        const size_t cl = kl.find("content-length:");
        if (cl != std::string::npos) koerper = (size_t) atoi(kl.c_str() + cl + 15);
      }
    }
  }
  return kopf_ende != 0 && v.size() >= kopf_ende + koerper;
}

bool DockDirekt::http_relay(int c, std::vector<uint8_t> &vorab) {
  std::vector<std::string> ziele;
  {
    std::lock_guard<std::mutex> l(s_http_mtx);
    ziele = http_ziele_;
  }
  if (ziele.empty() || !init_) return false;
  if (vorab.empty() && !anfrage_lesen(c, vorab)) return false;
  const uint32_t t0 = millis();
  // Funk anfordern (loop() leiht ihn bei WLAN aus) und Sende-Besitz holen
  http_funk_wunsch_ = true;
  while (!http_funk_bereit_ && millis() - t0 < 500) vTaskDelay(pdMS_TO_TICKS(5));
  bool besitz = false;
  while (http_funk_bereit_ && !(besitz = besitz_nehmen(2)) && millis() - t0 < 1000) vTaskDelay(pdMS_TO_TICKS(3));
  auto ende = [&](bool r) {
    if (besitz) besitz_abgeben(2);
    http_funk_wunsch_ = false;
    return r;
  };
  if (!besitz) { ESP_LOGD(TAG, "HTTP (Dock): Funk nicht frei"); return ende(false); }

  http_leeren();
  const uint32_t id = esp_random() | 1;
  std::vector<uint8_t> p;
  uint8_t mac[6];
  bool gefunden = false;
  for (auto &z : ziele) {
    if (!mac_lesen(z.c_str(), mac)) continue;
    if (http_paket_(mac, HTTP_AUF, id, nullptr, 0, p) && roh_senden_(mac, p, 2)) { gefunden = true; break; }
  }
  if (!gefunden) { ESP_LOGD(TAG, "HTTP (Dock): kein Dock erreicht"); return ende(false); }

  // Anfrage (Remote -> Dock) und Antwort (Dock -> Remote) mit Versatz und Quittung
  uint32_t req_gesendet = 0, req_quittiert = 0, rx = 0;
  uint32_t letzte = millis(), req_ms = 0;
  bool fertig = false, etwas = false, remote_zu = false;
  uint8_t buf[HTTP_STUECK + 4];
  std::vector<uint8_t> anfrage = vorab;
  std::string antwort_zeile;   // fuers Protokoll: Statuszeile von HA (2026-10-25)
  int wdh = 0;
  while (!fertig && millis() - letzte < 3000) {
    bool aktiv = false;
    // Anfrage-Bytes senden (Fenster)
    while (req_gesendet < anfrage.size() && req_gesendet - req_quittiert < HTTP_FENSTER) {
      const size_t n = std::min<size_t>(HTTP_STUECK, anfrage.size() - req_gesendet);
      u32_setzen(buf, req_gesendet);
      memcpy(buf + 4, anfrage.data() + req_gesendet, n);
      if (!http_paket_(mac, HTTP_DATEN, id, buf, n + 4, p) || !roh_senden_(mac, p, 3)) break;
      req_gesendet += n;
      req_ms = millis();
      aktiv = true;
    }
    if (req_quittiert < req_gesendet && millis() - req_ms > 250) {   // nichts quittiert: ab der Quittung neu
      if (++wdh > 8) break;
      req_gesendet = req_quittiert;
      continue;
    }
    // Pakete vom Dock
    HttpRx r;
    while (http_holen(r)) {
      uint8_t typ;
      uint32_t rid;
      std::vector<uint8_t> in;
      if (memcmp(r.mac, mac, 6) != 0 || !http_lesen_(r.mac, r.d, typ, rid, in) || rid != id) continue;
      aktiv = true;
      if (typ == HTTP_QUITT && in.size() >= 4) {
        const uint32_t q = u32_lesen(in.data());
        if (q > req_quittiert) { req_quittiert = q; wdh = 0; }
      } else if (typ == HTTP_DATEN && in.size() >= 4) {
        const uint32_t off = u32_lesen(in.data());
        if (off == rx) {
          if (rx == 0) antwort_zeile.assign((const char *) in.data() + 4, std::min<size_t>(in.size() - 4, 40));
          size_t o = 4;
          while (o < in.size()) {
            const int w = lwip_send(c, in.data() + o, in.size() - o, 0);
            if (w <= 0) { remote_zu = true; break; }
            o += w;
          }
          rx += in.size() - 4;
          etwas = true;
        }
        u32_setzen(buf, rx);
        if (http_paket_(mac, HTTP_QUITT, id, buf, 4, p)) roh_senden_(mac, p, 1);
      } else if (typ == HTTP_ZU) {
        fertig = true;
      }
      if (remote_zu) break;
    }
    // Lokaler Abrufer fertig (Socket zu)?
    if (!remote_zu && req_gesendet >= anfrage.size()) {
      fd_set rs;
      FD_ZERO(&rs);
      FD_SET(c, &rs);
      struct timeval tv = {0, 0};
      if (lwip_select(c + 1, &rs, nullptr, nullptr, &tv) > 0) {
        uint8_t m[512];
        const int k = lwip_recv(c, m, sizeof(m), 0);
        if (k <= 0) remote_zu = true;
        else { anfrage.insert(anfrage.end(), m, m + k); aktiv = true; }   // weitere Anfrage (keep-alive)
      }
    }
    if (remote_zu) break;
    if (aktiv) letzte = millis();
    else vTaskDelay(pdMS_TO_TICKS(2));
  }
  if (http_paket_(mac, HTTP_ZU, id, nullptr, 0, p)) roh_senden_(mac, p, 2);
  // Anfragezeile (GET /pfad) und Statuszeile mitschreiben - bei falschen Antworten sonst nur eine Byte-Zahl
  std::string anfrage_zeile((const char *) anfrage.data(), std::min<size_t>(anfrage.size(), 90));
  for (std::string *z : {&anfrage_zeile, &antwort_zeile}) {
    const size_t e = z->find('\r');
    if (e != std::string::npos) z->resize(e);
  }
  ESP_LOGI(TAG, "HTTP (Dock %02X:%02X:%02X:%02X:%02X:%02X): %u Bytes in %u ms%s - %s -> %s", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5], (unsigned) rx, (unsigned) (millis() - t0), fertig || remote_zu ? "" : " - abgebrochen",
           anfrage_zeile.c_str(), antwort_zeile.c_str());
  if (etwas) ok_ms_ = millis() | 1;
  return ende(etwas);
}

#ifdef DD_HA
void DockDirekt::http_dock_task_() {
  for (;;) {
    HttpRx r;
    if (!http_holen(r)) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    uint8_t typ;
    uint32_t id;
    std::vector<uint8_t> in;
    if (!http_lesen_(r.mac, r.d, typ, id, in) || typ != HTTP_AUF) continue;   // Reste alter Abrufe
    // Wiederholschutz: gleiche Sitzung nur mit steigender Nummer
    const uint32_t sitzung = u32_lesen(r.d.data() + 4), nr = u32_lesen(r.d.data() + 8);
    HttpPeer *hp = nullptr;
    for (auto &x : http_peers_) if (memcmp(x.mac, r.mac, 6) == 0) hp = &x;
    if (hp != nullptr && hp->sitzung == sitzung && nr <= hp->nr) continue;
    if (hp == nullptr) {
      if (http_peers_.size() >= 8) http_peers_.erase(http_peers_.begin());
      http_peers_.push_back(HttpPeer{});
      hp = &http_peers_.back();
      memcpy(hp->mac, r.mac, 6);
    }
    hp->sitzung = sitzung;
    hp->nr = nr;
    uint8_t mac[6];
    memcpy(mac, r.mac, 6);
    const uint32_t t0 = millis();

    // Mit HAs HTTP verbinden
    const size_t k = ha_http_.find(':');
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(k == std::string::npos ? 8123 : (uint16_t) atoi(ha_http_.c_str() + k + 1));
    const std::string host = ha_http_.substr(0, k);
    a.sin_addr.s_addr = ipaddr_addr(host.c_str());
    if (a.sin_addr.s_addr == IPADDR_NONE) {   // Name statt IP (z. B. homeassistant.local): aufloesen
      struct addrinfo hints = {}, *res = nullptr;
      hints.ai_family = AF_INET;
      if (lwip_getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res != nullptr) {
        a.sin_addr = ((struct sockaddr_in *) res->ai_addr)->sin_addr;
        lwip_freeaddrinfo(res);
      }
    }
    int u = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct timeval to = {2, 0};
    if (u >= 0) {
      lwip_setsockopt(u, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
      lwip_setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    }
    bool besitz = false;
    while (!(besitz = besitz_nehmen(2)) && millis() - t0 < 500) vTaskDelay(pdMS_TO_TICKS(3));
    std::vector<uint8_t> p;
    if (u < 0 || !besitz || lwip_connect(u, (struct sockaddr *) &a, sizeof(a)) != 0) {
      ESP_LOGW(TAG, "HTTP fuer %02X:%02X:%02X:%02X:%02X:%02X: keine Verbindung zu HA (%s)", mac[0], mac[1], mac[2], mac[3],
               mac[4], mac[5], ha_http_.c_str());
      if (besitz && http_paket_(mac, HTTP_ZU, id, nullptr, 0, p)) roh_senden_(mac, p, 2);
      if (u >= 0) lwip_close(u);
      if (besitz) besitz_abgeben(2);
      continue;
    }
    uint32_t req_rx = 0, gesendet = 0, quittiert = 0, letzte = millis(), fortschritt = millis();
    // Antwort-Bytes ab "quittiert", noch nicht bestaetigt - fester Puffer: ein wachsender Vektor lief auf dem C3 in
    // bad_alloc (Absturz 2026-10-21)
    static uint8_t offen[HTTP_FENSTER];
    size_t offen_n = 0;
    bool ha_zu = false, remote_zu = false;
    int wdh = 0;
    uint8_t buf[HTTP_STUECK + 4];
    while (millis() - letzte < 3000) {
      bool aktiv = false;
      HttpRx x;
      while (http_holen(x)) {
        uint8_t t2;
        uint32_t id2;
        std::vector<uint8_t> d2;
        if (memcmp(x.mac, mac, 6) != 0 || !http_lesen_(x.mac, x.d, t2, id2, d2) || id2 != id) continue;
        aktiv = true;
        if (t2 == HTTP_DATEN && d2.size() >= 4) {
          if (u32_lesen(d2.data()) == req_rx) {
            size_t o = 4;
            while (o < d2.size()) {
              const int w = lwip_send(u, d2.data() + o, d2.size() - o, 0);
              if (w <= 0) { ha_zu = true; break; }
              o += w;
            }
            req_rx += d2.size() - 4;
          }
          u32_setzen(buf, req_rx);
          if (http_paket_(mac, HTTP_QUITT, id, buf, 4, p)) roh_senden_(mac, p, 1);
        } else if (t2 == HTTP_QUITT && d2.size() >= 4) {
          const uint32_t q = u32_lesen(d2.data());
          if (q > quittiert && q <= quittiert + offen_n) {
            memmove(offen, offen + (q - quittiert), offen_n - (q - quittiert));
            offen_n -= q - quittiert;
            quittiert = q;
            if (gesendet < quittiert) gesendet = quittiert;
            fortschritt = millis();
            wdh = 0;
          }
        } else if (t2 == HTTP_ZU) {
          remote_zu = true;
        }
      }
      if (remote_zu) break;
      // Antwort von HA holen, solange das Fenster Platz hat
      if (!ha_zu && offen_n < HTTP_FENSTER) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(u, &rs);
        struct timeval tv = {0, 0};
        if (lwip_select(u + 1, &rs, nullptr, nullptr, &tv) > 0) {
          const int n = lwip_recv(u, offen + offen_n, std::min<size_t>(HTTP_STUECK, HTTP_FENSTER - offen_n), 0);
          if (n <= 0) ha_zu = true;
          else { offen_n += n; aktiv = true; }
        }
      }
      // Unbestaetigte Bytes senden
      while (gesendet < quittiert + offen_n) {
        const size_t von = gesendet - quittiert, n = std::min<size_t>(HTTP_STUECK, offen_n - von);
        u32_setzen(buf, gesendet);
        memcpy(buf + 4, offen + von, n);
        if (!http_paket_(mac, HTTP_DATEN, id, buf, n + 4, p) || !roh_senden_(mac, p, 3)) break;
        gesendet += n;
        aktiv = true;
      }
      if (gesendet > quittiert && millis() - fortschritt > 250) {   // keine Quittung: ab der letzten neu
        if (++wdh > 12) break;
        gesendet = quittiert;
        fortschritt = millis();
      }
      if (ha_zu && offen_n == 0) {
        if (http_paket_(mac, HTTP_ZU, id, nullptr, 0, p)) roh_senden_(mac, p, 2);
        break;
      }
      if (aktiv) letzte = millis();
      else vTaskDelay(pdMS_TO_TICKS(2));
    }
    lwip_close(u);
    besitz_abgeben(2);
    ESP_LOGI(TAG, "HTTP fuer %02X:%02X:%02X:%02X:%02X:%02X: %u Bytes in %u ms (frei %u KB, groesster Block %u KB)", mac[0],
             mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned) quittiert, (unsigned) (millis() - t0),
             (unsigned) (heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
  }
}
#else
void DockDirekt::http_dock_task_() {}
#endif

}  // namespace dock_direkt
}  // namespace esphome
