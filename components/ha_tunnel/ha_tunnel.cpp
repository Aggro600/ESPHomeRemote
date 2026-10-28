#include "ha_tunnel.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cerrno>
#include <cstring>
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esphome/components/network/util.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/api/api_server.h"

namespace esphome {
namespace ha_tunnel {

static const char *const TAG = "ha_tunnel";
// Hoechstens so viele Notifies pro Schleifenrunde - der Rest kommt in der naechsten.
static const int MAX_FRAMES_PER_LOOP = 24;

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static uint32_t get_u32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void http_task_entry(void *arg) { static_cast<HaTunnel *>(arg)->http_task_(); }

void HaTunnel::http_setup_() {
  if (http_port_ == 0) return;
  http_listen_ = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (http_listen_ < 0) { ESP_LOGE(TAG, "HTTP: socket() fehlgeschlagen"); return; }
  int one = 1;
  lwip_setsockopt(http_listen_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(http_port_);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (lwip_bind(http_listen_, (struct sockaddr *) &a, sizeof(a)) != 0 || lwip_listen(http_listen_, 4) != 0) {
    ESP_LOGE(TAG, "HTTP: 127.0.0.1:%u belegt (errno %d)", http_port_, errno);
    lwip_close(http_listen_);
    http_listen_ = -1;
    return;
  }
  // Stapel bewusst im INTERNEN RAM: dieser Task leitet auch Updates weiter. Zu Beginn eines Updates loescht ESPHome den
  // Flash-Bereich, solange ist der PSRAM-Zugriff gesperrt - mit Stapel im PSRAM blieb das Update nach dem Handshake
  // haengen (2026-10-06, zweimal "101 Bytes", dann Zeitueberschreitung). Nicht ins PSRAM verlegen.
  if (xTaskCreate(http_task_entry, "halink_http", 6144, this, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "HTTP-Task nicht gestartet");
    return;
  }
  {
    ip4_addr_t a{ha_ip_.load()};
    ESP_LOGI(TAG, "HTTP-Vermittler auf 127.0.0.1:%u -> %s:%u (WLAN) bzw. Tunnel", http_port_, ip4addr_ntoa(&a), (unsigned) ha_port_.load());
  }
}

// Ein Rahmen auf den Link, notfalls warten (link_send ist gesperrt/threadsicher).
bool HaTunnel::http_send_link_(uint8_t type, const uint8_t *d, size_t n, uint32_t timeout_ms) {
  uint8_t buf[516];
  if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
  buf[0] = type;
  if (n) memcpy(buf + 1, d, n);
  const uint32_t t0 = millis();
  while (!kb_->link_send(buf, n + 1)) {
    if (!kb_->link_connected() || millis() - t0 > timeout_ms) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return true;
}

void HaTunnel::http_task_() {
  for (;;) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(http_listen_, &r);
    struct timeval tv = {0, 100000};   // 100 ms: dazwischen auf ein Update (0x60) von HA schauen
    if (lwip_select(http_listen_ + 1, &r, nullptr, nullptr, &tv) <= 0) {
      std::vector<uint8_t> f;
      while (kb_->link_pop_http(f)) {
        if (!f.empty() && f[0] == 0x60) { ota_relay_(); break; }
      }
      if (http_fast_ && millis() - http_last_ms_ > 5000) {   // nach 5 s ohne HTTP wieder sparsam
        http_fast_ = false;
        idle_interval_();
      }
      continue;
    }
    struct sockaddr_in src;
    socklen_t sl = sizeof(src);
    int c = lwip_accept(http_listen_, (struct sockaddr *) &src, &sl);
    if (c < 0) continue;
    http_last_ms_ = millis();
    auto *w = wifi::global_wifi_component;
    // Reihenfolge der Wege (2026-10-20): Dock zuerst (Einstellung) oder nur als Ausweg. Hat der Dock-Weg die Anfrage
    // schon gelesen, aber kein Dock geantwortet, bekommt der naechste Weg sie aus "vorab".
    // WLAN verbunden (2026-10-25): immer zuerst WLAN - die Belegung (~65 kB) brauchte ueber das Dock ~1 min und brach
    // oft ab, ueber WLAN 1-2 s. "Dock zuerst" gilt nur ohne WLAN.
    std::vector<uint8_t> vorab;
    const bool wlan = w != nullptr && w->is_connected() && ha_ip_.load() != 0;
    bool fertig = !wlan && http_weg_.load() == 1 && http_dock_ && http_dock_(c, vorab);
    if (!fertig) {
      if (wlan) http_wlan_(c, vorab);
      else if (kb_->link_connected()) http_ble_(c, vorab);
      else if (!(http_weg_.load() == 0 && http_dock_ && http_dock_(c, vorab))) {
        ESP_LOGW(TAG, "HTTP: kein Weg (weder WLAN noch Link noch Dock)");
        if (ohne_weg) ohne_weg();
      }
    }
    lwip_close(c);
    http_last_ms_ = millis();
  }
}

void HaTunnel::http_wlan_(int c, const std::vector<uint8_t> &vorab) {
  const uint32_t t0 = millis();
  int u = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (u < 0) return;
  struct timeval to = {3, 0};
  lwip_setsockopt(u, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  const uint32_t ip = ha_ip_.load(), port = ha_port_.load();
  if (ip == 0) { lwip_close(u); return; }   // Adresse noch unbekannt (HA kuendigt sie gleich an)
  a.sin_port = htons((uint16_t) port);
  a.sin_addr.s_addr = ip;
  if (lwip_connect(u, (struct sockaddr *) &a, sizeof(a)) != 0) {
    ESP_LOGW(TAG, "HTTP (WLAN): Verbindung zu Port %u fehlgeschlagen (errno %d)", (unsigned) port, errno);
    lwip_close(u);
    return;
  }
  if (!vorab.empty() && lwip_send(u, vorab.data(), vorab.size(), 0) != (int) vorab.size()) { lwip_close(u); return; }
  uint8_t buf[1024];
  uint32_t n = 0;
  for (;;) {
    fd_set r;
    FD_ZERO(&r);
    FD_SET(c, &r);
    FD_SET(u, &r);
    struct timeval tv = {15, 0};
    if (lwip_select(std::max(c, u) + 1, &r, nullptr, nullptr, &tv) <= 0) break;
    if (FD_ISSET(c, &r)) {
      int k = lwip_recv(c, buf, sizeof(buf), 0);
      if (k <= 0 || lwip_send(u, buf, k, 0) != k) break;
    }
    if (FD_ISSET(u, &r)) {
      int k = lwip_recv(u, buf, sizeof(buf), 0);
      if (k <= 0 || lwip_send(c, buf, k, 0) != k) break;
      n += k;
    }
  }
  lwip_close(u);
  ESP_LOGD(TAG, "HTTP (WLAN): %u Bytes in %u ms", (unsigned) n, (unsigned) (millis() - t0));
}

void HaTunnel::ota_relay_() {
  const uint32_t t0 = millis();
  int s = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(ota_port_);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (s < 0 || lwip_connect(s, (struct sockaddr *) &a, sizeof(a)) != 0) {
    if (s >= 0) lwip_close(s);
    http_send_link_(0x63, nullptr, 0, 1000);
    ESP_LOGW(TAG, "OTA: lokaler OTA-Server (Port %u) nicht erreichbar", ota_port_);
    return;
  }
  http_fast_ = true;
  kb_->set_link_interval(6, 12, 0);   // Firmware: ~17 KB/s
  http_send_link_(0x61, nullptr, 0, 2000);
  uint32_t rx = 0, ack = 0, last = millis();
  uint8_t buf[512];
  const size_t room = std::min<size_t>(kb_->link_payload() - 1, sizeof(buf));
  std::vector<uint8_t> f;
  bool zu = false;
  while (!zu && millis() - last < 30000 && kb_->link_connected()) {
    bool aktiv = false;
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    struct timeval tv = {0, 0};
    if (lwip_select(s + 1, &r, nullptr, nullptr, &tv) > 0) {   // Antworten des OTA-Servers -> HA
      int k = lwip_recv(s, buf, room, 0);
      if (k <= 0) break;
      if (!http_send_link_(0x62, buf, k, 5000)) break;
      aktiv = true;
    }
    while (kb_->link_pop_http(f) && !f.empty()) {   // Firmware von HA -> OTA-Server
      aktiv = true;
      if (f[0] == 0x62) {
        size_t off = 1;
        while (off < f.size()) {
          int w = lwip_send(s, f.data() + off, f.size() - off, 0);
          if (w <= 0) { zu = true; break; }
          off += w;
        }
        rx += f.size() - 1;
      } else if (f[0] == 0x63) {
        zu = true;
      }
      if (zu) break;
    }
    if (rx - ack >= 2048 || (rx != ack && !aktiv)) {
      uint8_t b[4];
      put_u32(b, rx);
      if (http_send_link_(0x64, b, 4, 1000)) ack = rx;
    }
    if (aktiv) last = millis();
    else vTaskDelay(pdMS_TO_TICKS(2));
    http_last_ms_ = millis();
  }
  lwip_close(s);
  http_send_link_(0x63, nullptr, 0, 1000);
  ESP_LOGI(TAG, "OTA ueber Bluetooth: %u Bytes in %u ms", (unsigned) rx, (unsigned) (millis() - t0));
}

void HaTunnel::http_ble_(int c, const std::vector<uint8_t> &vorab) {
  const uint32_t t0 = millis();
  std::vector<uint8_t> f;
  while (kb_->link_pop_http(f)) {}   // Reste einer frueheren Abfrage
  // Schneller Takt (7,5-15 ms, ~17 KB/s) erst, wenn die Antwort groesser als 512 Bytes wird (Bilder), s. unten. Kleine Abfragen
  // (Menue-Zustaende alle 5 s, ~300 Bytes) hielten die Verbindung sonst bei offenem Menue dauerhaft im schnellen
  // Takt - kostete Prozessorzeit (Menue zaeh) und Strom (2026-10-07).
  if (!http_send_link_(0x43, nullptr, 0, 2000)) return;
  bool offen = false;
  while (millis() - t0 < 10000 && !offen) {
    if (kb_->link_pop_http(f) && !f.empty()) {
      if (f[0] == 0x41) offen = true;
      // 0x42 hier = verspaetetes "zu" einer frueheren Anfrage (2026-10-07) - nicht als Absage werten, sonst haengt
      // die neue Verbindung halb offen und jede weitere Anfrage scheitert genauso. Echte Absage: 10-s-Grenze.
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  if (!offen) {
    ESP_LOGW(TAG, "HTTP (Tunnel): HA hat nicht geantwortet");
    return;
  }
  uint32_t rx = 0, ack = 0, last = millis();
  bool ha_zu = false, remote_zu = false;
  uint8_t buf[512];
  const size_t room = std::min<size_t>(kb_->link_payload() - 1, sizeof(buf));
  for (size_t o = 0; o < vorab.size(); o += room)   // vom Dock-Weg schon gelesene Anfrage zuerst
    if (!http_send_link_(0x40, vorab.data() + o, std::min(room, vorab.size() - o), 5000)) return;
  while (millis() - last < 20000) {
    bool aktiv = false;
    // Remote-Komponente -> HA (die Anfrage)
    if (!remote_zu) {
      fd_set r;
      FD_ZERO(&r);
      FD_SET(c, &r);
      struct timeval tv = {0, 0};
      if (lwip_select(c + 1, &r, nullptr, nullptr, &tv) > 0) {
        int k = lwip_recv(c, buf, room, 0);
        if (k <= 0) remote_zu = true;
        else if (!http_send_link_(0x40, buf, k, 5000)) break;
        aktiv = true;
      }
    }
    // HA -> Remote-Komponente (die Antwort)
    while (kb_->link_pop_http(f) && !f.empty()) {
      aktiv = true;
      if (f[0] == 0x40) {
        if (lwip_send(c, f.data() + 1, f.size() - 1, 0) < 0) { remote_zu = true; break; }
        rx += f.size() - 1;
        if (rx > 512 && !http_fast_) {   // groessere Antwort (Bild): sofort schnell - Menue-Zustaende (~300 B) bleiben sparsam
          http_fast_ = true;
          // Breiter Bereich 7,5-50 ms (2026-10-20): ein Proxy mit 3 Verbindungen lehnte 7,5-15 ms ab (Status 13/16),
          // Remote 2 blieb bei 146 ms Latenz 3 - Bilder 25 s statt 1 s. Aus dem Bereich nimmt der Proxy, was er schafft.
          kb_->set_link_interval(6, 40, 0);
        }
      } else if (f[0] == 0x42) {
        ha_zu = true;
      }
    }
    if (rx - ack >= 1024 || (rx != ack && !aktiv)) {   // Fenster: HA darf weiterschicken
      uint8_t b[4];
      put_u32(b, rx);
      // Quittung: der Link schickt immer nur ein Paket gleichzeitig - im Ruhetakt dauert ein freier Platz, also
      // laenger warten als 1 s (vorher brach HA nach 10 s ohne Quittung ab, Bilder blieben bei 4 KB haengen)
      if (http_send_link_(0x44, b, 4, 4000)) ack = rx;
    }
    if (ha_zu || remote_zu || !kb_->link_connected()) break;
    if (aktiv) last = millis();
    else vTaskDelay(pdMS_TO_TICKS(3));
    http_last_ms_ = millis();
  }
  if (!ha_zu) http_send_link_(0x42, nullptr, 0, 1000);
  ESP_LOGI(TAG, "HTTP (Tunnel): %u Bytes in %u ms", (unsigned) rx, (unsigned) (millis() - t0));
}

// ── Sprache: IMA-ADPCM (Standardtabellen), 256 Samples je Rahmen ──────────────
static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
static const int8_t IMA_INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const size_t VOICE_SAMPLES_PER_FRAME = 256;

static uint8_t ima_encode(int16_t sample, int16_t &pred, int8_t &index) {
  int step = IMA_STEP[index];
  int diff = sample - pred;
  uint8_t code = 0;
  if (diff < 0) { code = 8; diff = -diff; }
  int delta = step >> 3;
  if (diff >= step) { code |= 4; diff -= step; delta += step; }
  step >>= 1;
  if (diff >= step) { code |= 2; diff -= step; delta += step; }
  step >>= 1;
  if (diff >= step) { code |= 1; delta += step; }
  int p = (code & 8) ? pred - delta : pred + delta;
  pred = (int16_t) std::max(-32768, std::min(32767, p));
  index = (int8_t) std::max(0, std::min(88, index + IMA_INDEX[code]));
  return code;
}

bool HaTunnel::voice_start() {
  if (mic_ == nullptr || !kb_->link_connected() || voice_ring_ == nullptr) {
    notice_(std::string("Sprache: Start abgelehnt (") + (mic_ == nullptr ? "kein Mikrofon" :
            voice_ring_ == nullptr ? "kein Puffer" : "kein Link") + ")");
    return false;
  }
  {
    std::lock_guard<std::mutex> g(voice_mtx_);
    voice_head_ = voice_tail_ = 0;
    voice_frames_ = voice_dropped_ = 0;
    voice_frame_.clear();
    adpcm_pred_ = 0;
    adpcm_index_ = 0;
    voice_end_pending_ = false;
  }
  const uint8_t codec = 1;
  send_frame_(0x50, &codec, 1);
  kb_->set_link_interval(6, 12, 0);   // 8 KB/s Audio - schnelles Intervall
  voice_on_ = true;
  mic_->start();
  high_freq_.start();
  notice_("Sprache: Aufnahme laeuft");
  return true;
}

void HaTunnel::voice_stop() {
  if (!voice_on_) return;
  voice_on_ = false;
  mic_->stop();
  std::lock_guard<std::mutex> g(voice_mtx_);
  voice_end_pending_ = true;   // 0x52 geht raus, sobald die Audio-Rahmen draussen sind
}

void HaTunnel::voice_mic_data_(const std::vector<uint8_t> &data) {
  if (!voice_on_) return;
  const auto info = mic_->get_audio_stream_info();
  const uint8_t bits = info.get_bits_per_sample();
  const uint8_t channels = info.get_channels() == 0 ? 1 : info.get_channels();
  const size_t bps = bits / 8;
  if (bps == 0) return;
  const size_t frames = data.size() / (bps * channels);
  std::lock_guard<std::mutex> g(voice_mtx_);
  for (size_t i = 0; i < frames; i++) {
    const uint8_t *p = data.data() + i * bps * channels;
    int32_t s;
    if (bits == 16) s = (int16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
    else if (bits == 32) s = ((int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24))) >> 16;
    else continue;
    s = (int32_t) (s * voice_gain_);
    const int16_t smp = (int16_t) std::max(-32768, std::min(32767, (int) s));
    if (voice_frame_.empty()) {   // Kopf: Startzustand des Decoders, damit jeder Rahmen einzeln dekodierbar ist
      voice_frame_.reserve(4 + VOICE_SAMPLES_PER_FRAME / 2);
      voice_frame_.push_back(0x51);
      voice_frame_.push_back((uint8_t) (adpcm_pred_ & 0xFF));
      voice_frame_.push_back((uint8_t) ((uint16_t) adpcm_pred_ >> 8));
      voice_frame_.push_back((uint8_t) adpcm_index_);
      nibble_hoch_ = false;
    }
    const uint8_t code = ima_encode(smp, adpcm_pred_, adpcm_index_);
    if (!nibble_hoch_) voice_frame_.push_back(code);   // erstes Sample ins untere Nibble
    else voice_frame_.back() |= (uint8_t) (code << 4);
    nibble_hoch_ = !nibble_hoch_;
    if (!nibble_hoch_ && voice_frame_.size() - 4 >= VOICE_SAMPLES_PER_FRAME / 2) {
      const size_t next = (voice_head_ + 1) % VOICE_RING;
      if (next == voice_tail_) {   // voll (6 s Rueckstand) - aeltesten verwerfen
        voice_tail_ = (voice_tail_ + 1) % VOICE_RING;
        voice_dropped_++;
      }
      memcpy(voice_ring_ + voice_head_ * VOICE_FRAME_MAX, voice_frame_.data(), voice_frame_.size());
      voice_len_[voice_head_] = (uint16_t) voice_frame_.size();
      voice_head_ = next;
      voice_frames_++;
      voice_frame_.clear();
    }
  }
}

void HaTunnel::voice_pump_() {
  std::lock_guard<std::mutex> g(voice_mtx_);
  for (int i = 0; i < 16 && voice_tail_ != voice_head_; i++) {
    if (!kb_->link_send(voice_ring_ + voice_tail_ * VOICE_FRAME_MAX, voice_len_[voice_tail_])) return;
    voice_tail_ = (voice_tail_ + 1) % VOICE_RING;
  }
  if (voice_end_pending_ && voice_tail_ == voice_head_) {
    const uint8_t e = 0x52;
    if (kb_->link_send(&e, 1)) {
      voice_end_pending_ = false;
      char b[80];
      snprintf(b, sizeof(b), "Sprache: Aufnahme beendet, %u Rahmen (%.1f s), %u verworfen", (unsigned) voice_frames_,
               voice_frames_ * 0.016f, (unsigned) voice_dropped_);
      notice_(b);
    }
  }
}

void HaTunnel::setup() {
  ble_only_pref_ = global_preferences->make_preference<bool>(0x48A1B0E5UL);
  bool gespeichert = false;
  if (ble_only_pref_.load(&gespeichert) && gespeichert) {
    ble_only_ = true;
    api_rx_ms_ = millis();
    ESP_LOGI(TAG, "Nur Bluetooth: AN (gespeichert)");
  } else {
    ESP_LOGI(TAG, "Nur Bluetooth: AUS (gespeichert)");
  }
  voice_ring_ = (uint8_t *) heap_caps_malloc(VOICE_RING * VOICE_FRAME_MAX, MALLOC_CAP_SPIRAM);
  if (mic_ != nullptr)
    mic_->add_data_callback([this](const std::vector<uint8_t> &data) { this->voice_mic_data_(data); });
  http_setup_();
  if (kb_ == nullptr || !kb_->link_enabled()) {
    ESP_LOGW(TAG, "Kein Proxy eingetragen - Tunnel aus");
    this->mark_failed();
  }
}


bool HaTunnel::send_frame_(uint8_t type, const uint8_t *data, size_t len) {
  std::vector<uint8_t> f(1 + len);
  f[0] = type;
  if (len) memcpy(f.data() + 1, data, len);
  // Quittungen zusammenfassen: eine neuere ersetzt eine noch wartende
  if (type == 0x31 || type == 0x44) {
    for (auto &q : ctl_out_)
      if (!q.empty() && q[0] == type) { q.swap(f); return true; }
  }
  if (ctl_out_.size() >= 64) {
    ESP_LOGW(TAG, "Steuer-Warteschlange voll - aeltesten Rahmen (0x%02X) verworfen", ctl_out_.front()[0]);
    ctl_out_.pop_front();
  }
  ctl_out_.push_back(std::move(f));
  flush_ctl_();
  return true;
}

bool HaTunnel::flush_ctl_() {
  while (!ctl_out_.empty()) {
    auto &f = ctl_out_.front();
    if (!kb_->link_send(f.data(), f.size())) return false;
    ctl_out_.pop_front();
  }
  return true;
}

void HaTunnel::notice_(const std::string &msg) {
  ESP_LOGI(TAG, "%s", msg.c_str());
  send_frame_(0x20, (const uint8_t *) msg.data(), msg.size());
}

void HaTunnel::open_() {
  if (sock_ >= 0) close_("neu geoeffnet");
  sock_ = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_ < 0) {
    notice_("socket() fehlgeschlagen, errno " + std::to_string(errno));
    uint8_t z = 0;
    send_frame_(0x02, &z, 0);
    return;
  }
  int fl = lwip_fcntl(sock_, F_GETFL, 0);
  lwip_fcntl(sock_, F_SETFL, fl | O_NONBLOCK);
  int one = 1;
  lwip_setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port_);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  // VOR dem connect setzen: der API-Server nimmt die Loopback-Verbindung sofort an und prueft in seiner naechsten
  // Runde network::is_connected() - bei WLAN aus warf er den Client sonst raus, bevor pump_socket_() den Merker
  // setzte (Sitzungen, die bei schon ausgeschaltetem WLAN begannen, scheiterten so immer; 2026-10-05).
  network::g_link_session = true;
  int r = lwip_connect(sock_, (struct sockaddr *) &a, sizeof(a));
  if (r != 0 && errno != EINPROGRESS) {
    notice_("connect(127.0.0.1) fehlgeschlagen, errno " + std::to_string(errno));
    close_("connect");
    return;
  }
  connecting_ = true;
  to_sock_.clear();
  bytes_in_ = bytes_out_ = 0;
  reset_flow_();
}

void HaTunnel::close_(const char *why) {
  if (sock_ >= 0) {
    lwip_close(sock_);
    sock_ = -1;
    ESP_LOGI(TAG, "Sitzung zu (%s), %u Bytes rein, %u raus", why, (unsigned) bytes_in_, (unsigned) bytes_out_);
    send_frame_(0x02, nullptr, 0);
  }
  connecting_ = false;
  network::g_link_session = false;
  to_sock_.clear();
  high_freq_.stop();
}

void HaTunnel::handle_frame_(std::vector<uint8_t> &f) {
  if (f.empty()) return;
  const uint8_t t = f[0];
  const uint8_t *d = f.data() + 1;
  const size_t n = f.size() - 1;
  switch (t) {
    case 0x00:
      rx_total_ += n;
      if (sock_ >= 0) api_rx_ms_ = millis();
      if (sock_ >= 0) {
        to_sock_.insert(to_sock_.end(), d, d + n);
        bytes_in_ += n;
      }
      break;
    case 0x03: {
      // Daten mit Position (2026-10-23): HA schreibt ohne Bestaetigung, ein voller Proxy verwirft dabei Pakete
      // stillschweigend - fehlte ein Stueck des Verbindungsaufbaus, hing HA 60 s. HA wiederholt jetzt ab der letzten
      // Quittung; hier Doppeltes ueberspringen, nach einer Luecke sofort quittieren (HA schickt den Rest neu).
      if (n < 4) break;
      const uint32_t off = get_u32(d);
      const uint8_t *q = d + 4;
      size_t m = n - 4;
      if (off > rx_total_ || rx_total_ - off >= m) {   // Luecke oder schon gehabt
        uint8_t b[4];
        put_u32(b, rx_total_);
        send_frame_(0x31, b, 4);
        rx_ack_sent_ = rx_total_;
        break;
      }
      q += rx_total_ - off;
      m -= rx_total_ - off;
      rx_total_ += m;
      if (sock_ >= 0) {
        api_rx_ms_ = millis();
        to_sock_.insert(to_sock_.end(), q, q + m);
        bytes_in_ += m;
      }
      break;
    }
    case 0x01: open_(); break;
    case 0x02: close_("HA"); break;
    case 0x10: send_frame_(0x11, d, n); break;
    case 0x12:
      if (n >= 4) {
        reset_flow_();
        test_total_ = test_left_ = get_u32(d);
        test_t0_ = millis();
        high_freq_.start();
      }
      break;
    case 0x15:
      rx_total_ += n;
      if (sink_n_ == 0) sink_t0_ = millis();
      sink_n_ += n;
      sink_t1_ = millis();
      break;
    case 0x16: {
      uint8_t b[8];
      put_u32(b, sink_n_);
      put_u32(b + 4, sink_t1_ - sink_t0_);
      send_frame_(0x17, b, 8);
      sink_n_ = 0;
      break;
    }
    case 0x30:
      if (n >= 4) tx_acked_ = get_u32(d);
      break;
    case 0x32:
      if (n >= 4) window_ = std::max<uint32_t>(get_u32(d), 256);
      break;
    case 0x53:
      if (n >= 1) {
        std::string text((const char *) d + 1, n - 1);
        const int typ = d[0];
        if (typ == 4 || typ == 5) idle_interval_();
        for (auto &cb : voice_cbs_) cb(typ, text);
      }
      break;
    case 0x34:
      if (n >= 1 && wifi::global_wifi_component != nullptr) {
        if (d[0]) wifi::global_wifi_component->enable();
        else wifi::global_wifi_component->disable();
        notice_(d[0] ? "WLAN an" : "WLAN aus");
      }
      break;
    case 0x38: {
      uint8_t b[12 + 32];
      kb_->link_diag(b);
      uint32_t v[8] = {tx_total_, tx_acked_, rx_total_, (uint32_t) to_sock_.size(), (uint32_t) (pending_.size() * 1000 + ctl_out_.size()),
                       (uint32_t) (sock_ >= 0), (uint32_t) (wifi::global_wifi_component != nullptr &&
                                                            wifi::global_wifi_component->is_connected()),
                       loops_per_s_};
      memcpy(b + 12, v, sizeof(v));
      uint32_t res, age;
      memcpy(&res, b + 4, 4);
      memcpy(&age, b + 8, 4);
      ESP_LOGI(TAG, "Diagnose: inflight=%u retry=%u congested=%u sendable=%u resends=%u letztes Notify vor %u ms | "
               "tx %u acked %u rx %u to_sock %u pending %u socket %u wlan %u loops/s %u",
               b[0], b[1], b[2], b[3], (unsigned) res, (unsigned) age, (unsigned) v[0], (unsigned) v[1],
               (unsigned) v[2], (unsigned) v[3], (unsigned) v[4], (unsigned) v[5], (unsigned) v[6], (unsigned) v[7]);
      std::vector<uint8_t> fr(1 + sizeof(b));
      fr[0] = 0x39;
      memcpy(fr.data() + 1, b, sizeof(b));
      send_frame_(0x39, fr.data() + 1, fr.size() - 1);
      break;
    }
    case 0x35:
      if (n >= 2) {
        const bool hid = kb_->is_connected();
        if (hid) kb_->send_key_combo(d[0], d[1]);
        uint8_t b[2] = {(uint8_t) hid, (uint8_t) kb_->link_connected()};
        send_frame_(0x36, b, 2);
      }
      break;
    case 0x33:
      if (n >= 6) kb_->set_link_interval(d[0] | (d[1] << 8), d[2] | (d[3] << 8), d[4] | (d[5] << 8));
      break;
    default: break;
  }
}

void HaTunnel::pump_socket_() {
  if (sock_ < 0) return;
  if (connecting_) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(sock_, &w);
    struct timeval tv = {0, 0};
    if (lwip_select(sock_ + 1, nullptr, &w, nullptr, &tv) <= 0) return;
    int err = 0;
    socklen_t el = sizeof(err);
    lwip_getsockopt(sock_, SOL_SOCKET, SO_ERROR, &err, &el);
    if (err != 0) {
      notice_("Loopback-Verbindung abgelehnt, errno " + std::to_string(err));
      close_("connect");
      return;
    }
    connecting_ = false;
    high_freq_.start();
    // HA gleicht nach dem Verbinden alles ab (Entitaeten, Zustaende, Abos) - im Spartakt riss der Link dabei ab
    // (Remote 2 nach dem Update 06.10. 09:01, zweimal). Die ersten 20 s deshalb schnell.
    kb_->set_link_interval(6, 12, 0);
    session_fast_bis_ = millis() + 20000;
    ESP_LOGI(TAG, "Sitzung offen (127.0.0.1:%u)", port_);
    const uint8_t kann = 1;   // Bit 0: Datenrahmen 0x03 mit Position (Wiederholung, 2026-10-23)
    send_frame_(0x01, &kann, 1);
  }
  // Link -> API
  if (!to_sock_.empty()) {
    int w = lwip_send(sock_, to_sock_.data(), to_sock_.size(), 0);
    if (w > 0) to_sock_.erase(to_sock_.begin(), to_sock_.begin() + w);
    else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { close_("send"); return; }
  }
  // API -> Link
  const size_t room = kb_->link_payload() - 1;
  uint8_t buf[512];
  for (int i = 0; i < MAX_FRAMES_PER_LOOP && pending_.empty() && ctl_out_.empty(); i++) {
    const size_t want = std::min<size_t>({room, sizeof(buf) - 1, tx_room_()});
    if (want == 0) return;   // Fenster voll - auf Quittung (0x30) warten
    int r = lwip_recv(sock_, buf + 1, want, 0);
    if (r == 0) { close_("API hat geschlossen"); return; }
    if (r < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) close_("recv");
      return;
    }
    bytes_out_ += r;
    tx_total_ += r;
    buf[0] = 0x00;
    if (!kb_->link_send(buf, r + 1)) {
      pending_.assign(buf, buf + r + 1);
      return;
    }
  }
}

void HaTunnel::pump_test_() {
  if (test_left_ == 0) return;
  const size_t room = kb_->link_payload() - 1;
  uint8_t buf[512];
  buf[0] = 0x13;
  for (int i = 0; i < MAX_FRAMES_PER_LOOP && test_left_ > 0 && pending_.empty() && ctl_out_.empty(); i++) {
    size_t k = std::min<size_t>({room, sizeof(buf) - 1, test_left_, tx_room_()});
    if (k == 0) return;   // Fenster voll
    memset(buf + 1, (uint8_t) test_left_, k);
    if (!kb_->link_send(buf, k + 1)) return;   // spaeter nochmal, test_left_ unveraendert
    test_left_ -= k;
    tx_total_ += k;
  }
  if (test_left_ == 0) {
    uint8_t b[8];
    put_u32(b, test_total_);
    put_u32(b + 4, millis() - test_t0_);
    send_frame_(0x14, b, 8);
    if (sock_ < 0) high_freq_.stop();
  }
}

void HaTunnel::set_ble_only(bool on) {
  // Sofort ins Flash (2026-10-26): Remote 2 wachte morgens mehrmals mit "Nur Bluetooth: AUS" auf
  if (on != ble_only_) { ble_only_pref_.save(&on); global_preferences->sync(); }
  ble_only_ = on;
  api_rx_ms_ = millis();   // ab jetzt laeuft die 2-min-Frist
  ESP_LOGI(TAG, "Nur Bluetooth: %s", on ? "AN (WLAN aus)" : "AUS");
  if (!on && wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_disabled())
    wifi::global_wifi_component->enable();
}

void HaTunnel::ble_only_loop_() {
  auto *w = wifi::global_wifi_component;
  if (!ble_only_ || w == nullptr) return;
  // HA verbunden (egal ueber welchen Weg) zaehlt als erreichbar - und solange das WLAN an ist (Wartungsfenster),
  // kann die Remote ohnehin nicht verloren gehen: die Uhr laeuft nur bei WLAN aus. 3 min, damit ein HA-Neustart
  // die Rettung nicht ausloest (am 06.10. 07:39 passiert: HA startete neu, Modus schaltete sich ab).
  // Bluetooth-Funk bewusst aus (Schnelleinstellungen): keine Rettung - der Nutzer will die Remote offline
  if ((api::global_api_server != nullptr && api::global_api_server->is_connected()) || w->is_connected() ||
      kb_->funk_aus() || kb_->link_paused())   // HA-Link absichtlich pausiert (Ruhezustand): kein Alarm
    api_rx_ms_ = millis();
  // Keine HA-Verbindung: den Modus NICHT mehr abschalten (2026-10-18 - nach HA-Neustarts blieb "Nur Bluetooth"
  // dauerhaft aus, gegen den Willen des Nutzers). Die Rettung macht der Notanker der Remote (remote.yaml, interval 20s):
  // Wartungsfenster = WLAN 3 min, wiederholt, solange HA fehlt. Hier nur melden.
  if (millis() - api_rx_ms_ > 180000) {
    ESP_LOGW(TAG, "Nur Bluetooth: 3 min keine HA-Verbindung - Notanker der Remote oeffnet das WLAN zeitweise");
    api_rx_ms_ = millis();
  }
  if (wifi_erlaubt_) return;   // Wartungsfenster: WLAN darf an sein
  if (!w->is_disabled()) w->disable();   // Skripte der Remote fragen wlan_zugriff::g_nur_ble - das hier ist nur Reserve
}

void HaTunnel::idle_interval_() {
  if (ruhe_ && kb_->link_schlummer()) kb_->set_link_interval(800, 1000, 1);   // ~1-1,25 s, jedes 2. Ereignis
  else if (ruhe_) kb_->set_link_interval(96, 120, 3);
  else kb_->set_link_interval(80, 120, 0);
}

void HaTunnel::set_ruhe(bool r) {
  const bool sch = kb_->link_schlummer();
  if (r == ruhe_ && sch == schlummer_) return;
  ruhe_ = r;
  schlummer_ = sch;
  // Laufende Bilder/Sprache/Sitzungsaufbau behalten ihren schnellen Takt und kehren danach selbst hierher zurueck
  if (!voice_on_ && !http_fast_ && session_fast_bis_ == 0) idle_interval_();
}

void HaTunnel::loop() {
  // Dauerschleife (high_freq_) nur, solange Daten fliessen, und 0,3 s danach (2026-10-07). Vorher lief sie bei offener
  // Sitzung ununterbrochen - mit "Nur Bluetooth" also immer: der Prozessor kam nie in den Leichtschlaf
  // (pm_bericht: rtos1 97 %, light_sleep_counts 0). Neue Daten vom Link holt die normale Runde (16/50 ms) ab.
  {
    const uint32_t summe = bytes_in_ + bytes_out_;
    const bool arbeit = summe != hf_summe_ || !pending_.empty() || !to_sock_.empty() || !ctl_out_.empty() ||
                        voice_on_ || voice_end_pending_ || test_left_ > 0 || connecting_;
    if (arbeit) {
      hf_summe_ = summe;
      hf_bis_ = millis() + 300;
      high_freq_.start();
    } else if (hf_bis_ != 0 && (int32_t) (millis() - hf_bis_) >= 0) {
      hf_bis_ = 0;
      high_freq_.stop();
    }
  }
  ble_only_loop_();
  if (session_fast_bis_ != 0 && (int32_t) (millis() - session_fast_bis_) >= 0) {
    session_fast_bis_ = 0;
    if (!voice_on_ && !http_fast_) idle_interval_();
  }
  // Status alle 60 s ueber den Link (die Bruecke schreibt ihn ins HA-Log) - nur im Debug-Modus (2026-10-07)
  if (debug_ && (ble_only_ || sock_ >= 0) && millis() - status_ms_ > 60000 && kb_->link_connected()) {
    status_ms_ = millis();
    char b[160];
    snprintf(b, sizeof(b), "Status: Sitzung %s, rein %u, raus %u, API-Clients %s, WLAN %s, Nur-BLE %s, API-Verkehr vor %us",
             sock_ >= 0 ? (connecting_ ? "baut auf" : "offen") : "zu", (unsigned) bytes_in_, (unsigned) bytes_out_,
             api::global_api_server != nullptr && api::global_api_server->is_connected() ? "ja" : "nein",
             wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected() ? "an" : "aus",
             ble_only_ ? "an" : "aus", (unsigned) ((millis() - api_rx_ms_) / 1000));
    notice_(b);
  }
  loops_++;
  if (millis() - loops_t0_ >= 1000) { loops_per_s_ = loops_; loops_ = 0; loops_t0_ = millis(); }
  const bool linked = kb_->link_connected();
  if (linked != was_linked_) {
    was_linked_ = linked;
    ESP_LOGI(TAG, "Link %s (Nutzlast %u Bytes)", linked ? "bereit" : "weg", kb_->link_payload());
    wifi_reported_ = -1;
    if (!linked) {
      pending_.clear();
      test_left_ = 0;
      close_("Link weg");
      ctl_out_.clear();   // nach close_: dessen "Sitzung zu" geht ohnehin nicht mehr raus
    }
  }
  if (!linked) return;
  // Zuerst EINGEHENDE Rahmen (HA-Befehle, Quittungen der Bruecke): sie duerfen nie hinter einem vollen Ausgang warten.
  // Vorher stand das hier hinter dem Senden - konnte die Remote nichts loswerden (Fenster voll, Link belegt), blieben
  // genau die Quittungen liegen, die das Fenster wieder oeffnen, und HA-Befehle kamen Minuten spaeter an (2026-10-19).
  std::vector<uint8_t> f;
  bool more = false;
  for (int i = 0; i < 64; i++) {
    if (!kb_->link_pop(f)) break;
    handle_frame_(f);
    more = i == 63;
  }
  // Empfang quittieren: alle 1 KB oder sobald die Warteschlange leer ist
  if (rx_total_ != rx_ack_sent_ && (!more || rx_total_ - rx_ack_sent_ >= 1024)) {
    uint8_t b[4];
    put_u32(b, rx_total_);
    send_frame_(0x31, b, 4);
    rx_ack_sent_ = rx_total_;
  }
  // Dann senden: Steuerrahmen zuerst, dann Sprache, dann der liegengebliebene Datenrahmen
  if (!flush_ctl_()) return;
  if (voice_on_ || voice_end_pending_) voice_pump_();
  if (!pending_.empty()) {
    if (!kb_->link_send(pending_.data(), pending_.size())) return;
    pending_.clear();
  }
  // WLAN-Zustand melden (HA leitet dann ueber WLAN oder den Tunnel)
  const int8_t wifi = wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected();
  if (wifi != wifi_reported_) {
    uint8_t b = (uint8_t) wifi;
    wifi_reported_ = wifi;
    send_frame_(0x21, &b, 1);
  }
  pump_test_();
  pump_socket_();
}

}  // namespace ha_tunnel
}  // namespace esphome
