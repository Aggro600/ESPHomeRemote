#pragma once
// Durchsage per WLAN direkt an Home Assistant (2026-11-09, Nutzer: "WLAN auch").
// Bisher ging die Durchsage nur ueber die Bluetooth-Schicht (-halink, ha_tunnel::voice_http_start) - die einfache
// Firmware von GitHub hat die nicht, dort kam immer "Geht gerade nicht". Gleiches Verfahren wie dort, nur ohne Tunnel:
// Mikrofon -> IMA-ADPCM 16 kHz (Rahmen 0x51, i16 Startwert, u8 Index, 128 Byte = 256 Samples) -> HTTP-POST (chunked)
// an http://<HA>/api/esphomeremote/sprache/<name>?zweck=durchsage&k=<schluessel> (sprache.py). Antwort: Zeilen
// "<typ>|<text>" (1 = erkannt, 2 = laeuft, 4 = Fehler, 5 = fertig) - die Hauptschleife holt sie mit ereignisse() ab.
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esphome/components/microphone/microphone.h"

namespace durchsage_wlan {

static const size_t RAHMEN_MAX = 4 + 128;
static const size_t RING = 1920;   // 1920 x 16 ms = 30 s
static const float VERSTAERKUNG = 6.5f;   // wie voice_assistant / ha_tunnel

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
static const int8_t IMA_INDEX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

inline std::mutex mtx, ev_mtx;
inline uint8_t *ring = nullptr;
inline uint16_t *laenge = nullptr;
inline size_t kopf = 0, fuss = 0;
inline std::vector<uint8_t> rahmen;
inline int16_t pred = 0;
inline int8_t idx = 0;
inline bool hoch = false;
inline volatile bool an = false;       // Mikrofon nimmt auf
inline volatile bool ende = false;     // losgelassen: Rest senden, dann Antwort lesen
inline volatile bool laeuft = false;   // Task laeuft (bis die Antwort da ist)
inline bool cb_da = false;
inline esphome::microphone::Microphone *mic = nullptr;
inline std::deque<std::pair<int, std::string>> ev;
inline std::string host, pfad;

inline uint8_t ima(int16_t s, int16_t &p, int8_t &i) {
  int step = IMA_STEP[i];
  int diff = s - p;
  uint8_t code = 0;
  if (diff < 0) { code = 8; diff = -diff; }
  int delta = step >> 3;
  if (diff >= step) { code |= 4; diff -= step; delta += step; }
  step >>= 1;
  if (diff >= step) { code |= 2; diff -= step; delta += step; }
  step >>= 1;
  if (diff >= step) { code |= 1; delta += step; }
  const int n = (code & 8) ? p - delta : p + delta;
  p = (int16_t) std::max(-32768, std::min(32767, n));
  i = (int8_t) std::max(0, std::min(88, i + IMA_INDEX[code]));
  return code;
}

inline void melden(int typ, const std::string &text) {
  std::lock_guard<std::mutex> g(ev_mtx);
  ev.emplace_back(typ, text);
}

// Ereignisse fuer die Hauptschleife (Anzeige, Mikrofon-Strom)
inline std::deque<std::pair<int, std::string>> ereignisse() {
  std::deque<std::pair<int, std::string>> e;
  std::lock_guard<std::mutex> g(ev_mtx);
  e.swap(ev);
  return e;
}

inline void mic_daten(const std::vector<uint8_t> &data) {
  if (!an || mic == nullptr) return;
  const auto info = mic->get_audio_stream_info();
  const uint8_t bits = info.get_bits_per_sample();
  const uint8_t kanaele = info.get_channels() == 0 ? 1 : info.get_channels();
  const size_t bps = bits / 8;
  if (bps == 0) return;
  const size_t n = data.size() / (bps * kanaele);
  std::lock_guard<std::mutex> g(mtx);
  for (size_t k = 0; k < n; k++) {
    const uint8_t *q = data.data() + k * bps * kanaele;
    int32_t s;
    if (bits == 16) s = (int16_t) ((uint16_t) q[0] | ((uint16_t) q[1] << 8));
    else if (bits == 32) s = ((int32_t) ((uint32_t) q[0] | ((uint32_t) q[1] << 8) | ((uint32_t) q[2] << 16) | ((uint32_t) q[3] << 24))) >> 16;
    else continue;
    const int16_t smp = (int16_t) std::max(-32768, std::min(32767, (int) (s * VERSTAERKUNG)));
    if (rahmen.empty()) {   // Kopf: Startzustand des Decoders, jeder Rahmen einzeln dekodierbar
      rahmen.push_back(0x51);
      rahmen.push_back((uint8_t) (pred & 0xFF));
      rahmen.push_back((uint8_t) ((uint16_t) pred >> 8));
      rahmen.push_back((uint8_t) idx);
      hoch = false;
    }
    const uint8_t code = ima(smp, pred, idx);
    if (!hoch) rahmen.push_back(code);   // erstes Sample ins untere Nibble
    else rahmen.back() |= (uint8_t) (code << 4);
    hoch = !hoch;
    if (!hoch && rahmen.size() - 4 >= 128) {
      const size_t naechst = (kopf + 1) % RING;
      if (naechst == fuss) fuss = (fuss + 1) % RING;   // voll: aeltesten verwerfen
      memcpy(ring + kopf * RAHMEN_MAX, rahmen.data(), rahmen.size());
      laenge[kopf] = (uint16_t) rahmen.size();
      kopf = naechst;
      rahmen.clear();
    }
  }
}

inline bool alles_senden(int s, const uint8_t *d, size_t n) {
  while (n) {
    const int w = lwip_send(s, d, n, 0);
    if (w <= 0) return false;
    d += w;
    n -= w;
  }
  return true;
}

inline void task(void *) {
  const uint32_t t0 = esphome::millis();
  // host = "IP:Port" (Attribut "basis" von HA) oder der Platzhalter aus der YAML
  std::string h = host, port = "8123";
  const size_t dp = h.rfind(':');
  if (dp != std::string::npos) { port = h.substr(dp + 1); h = h.substr(0, dp); }
  int s = -1;
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *ai = nullptr;
  bool ok = lwip_getaddrinfo(h.c_str(), port.c_str(), &hints, &ai) == 0 && ai != nullptr;
  if (ok) {
    s = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok = s >= 0;
  }
  if (ok) {
    struct timeval to = {5, 0};
    lwip_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
    ok = lwip_connect(s, ai->ai_addr, ai->ai_addrlen) == 0;
  }
  if (ai != nullptr) lwip_freeaddrinfo(ai);
  if (ok) {
    const std::string k = "POST " + pfad + " HTTP/1.1\r\nHost: " + host + "\r\nContent-Type: application/octet-stream\r\n"
                          "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
    ok = alles_senden(s, (const uint8_t *) k.data(), k.size());
  }
  // Audio: je Runde bis zu 16 Rahmen als ein Chunk, bis losgelassen und alles draussen ist
  static const size_t JE = 16;
  uint8_t *buf = (uint8_t *) heap_caps_malloc(JE * RAHMEN_MAX + 16, MALLOC_CAP_SPIRAM);
  uint32_t bytes = 0;
  bool fertig = false;
  while (ok && buf != nullptr && !fertig) {
    size_t n = 0;
    {
      std::lock_guard<std::mutex> g(mtx);
      for (size_t i = 0; i < JE && fuss != kopf; i++) {
        memcpy(buf + 10 + n, ring + fuss * RAHMEN_MAX, laenge[fuss]);
        n += laenge[fuss];
        fuss = (fuss + 1) % RING;
      }
      if (ende && fuss == kopf) fertig = true;
    }
    if (n) {
      char hx[11];
      const int hl = snprintf(hx, sizeof(hx), "%X\r\n", (unsigned) n);
      memcpy(buf + 10 - hl, hx, hl);
      buf[10 + n] = '\r';
      buf[11 + n] = '\n';
      ok = alles_senden(s, buf + 10 - hl, hl + n + 2);
      bytes += n;
    } else if (!fertig) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (esphome::millis() - t0 > 45000) fertig = true;   // Sicherung: hoechstens 45 s
  }
  if (ok && fertig) ok = alles_senden(s, (const uint8_t *) "0\r\n\r\n", 5);
  if (buf != nullptr) heap_caps_free(buf);
  ESP_LOGI("durchsage", "WLAN: %u Bytes Audio in %u ms%s", (unsigned) bytes, (unsigned) (esphome::millis() - t0),
           ok ? "" : " - abgebrochen");
  // Antwort: Kopf, dann Chunks mit Zeilen "<typ>|<text>" (leere Zeilen = Lebenszeichen)
  bool gemeldet = false;
  if (ok) {
    struct timeval to = {1, 0};
    lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
    std::string roh, text;
    bool kopf_da = false, schluss = false;
    const uint32_t t_auf = esphome::millis();
    uint32_t letzte = t_auf;
    char b[256];
    while (!schluss && esphome::millis() - letzte < 8000 && esphome::millis() - t_auf < 90000) {
      const int k = lwip_recv(s, b, sizeof(b), 0);
      if (k == 0) break;
      if (k < 0) continue;
      letzte = esphome::millis();
      roh.append(b, k);
      if (!kopf_da) {
        const size_t e = roh.find("\r\n\r\n");
        if (e == std::string::npos) continue;
        if (roh.compare(0, 12, "HTTP/1.1 200") != 0 && roh.compare(0, 12, "HTTP/1.0 200") != 0) {
          melden(4, "Home Assistant: " + roh.substr(9, std::min<size_t>(roh.find('\r'), 40) - 9));
          gemeldet = true;
          break;
        }
        kopf_da = true;
        roh.erase(0, e + 4);
      }
      for (;;) {   // Chunks entpacken
        const size_t z = roh.find("\r\n");
        if (z == std::string::npos) break;
        const size_t n = strtoul(roh.c_str(), nullptr, 16);
        if (n == 0 && z > 0) { schluss = true; break; }
        if (roh.size() < z + 2 + n + 2) break;
        text.append(roh, z + 2, n);
        roh.erase(0, z + 2 + n + 2);
      }
      size_t nl;
      while ((nl = text.find('\n')) != std::string::npos) {
        const std::string zeile = text.substr(0, nl);
        text.erase(0, nl + 1);
        const size_t st = zeile.find('|');
        if (st == std::string::npos || st == 0) continue;
        const int typ = atoi(zeile.c_str());
        if (typ == 4 || typ == 5) gemeldet = true;
        melden(typ, zeile.substr(st + 1));
      }
    }
  } else {
    melden(4, "Keine Verbindung zu Home Assistant");
    gemeldet = true;
  }
  if (s >= 0) lwip_close(s);
  if (!gemeldet) melden(5, "");
  laeuft = false;
  vTaskDelete(nullptr);
}

// Aufnahme starten und senden (false: geht nicht - Mikrofon, Speicher oder laeuft noch)
inline bool start(esphome::microphone::Microphone *m, const std::string &ha, const std::string &p) {
  if (m == nullptr || laeuft || ha.empty()) return false;
  if (ring == nullptr) {
    ring = (uint8_t *) heap_caps_malloc(RING * RAHMEN_MAX, MALLOC_CAP_SPIRAM);
    laenge = (uint16_t *) heap_caps_calloc(RING, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (ring == nullptr || laenge == nullptr) {
      heap_caps_free(ring);
      heap_caps_free(laenge);
      ring = nullptr;
      laenge = nullptr;
      return false;
    }
  }
  mic = m;
  if (!cb_da) {
    m->add_data_callback([](const std::vector<uint8_t> &d) { mic_daten(d); });
    cb_da = true;
  }
  {
    std::lock_guard<std::mutex> g(mtx);
    kopf = fuss = 0;
    rahmen.clear();
    pred = 0;
    idx = 0;
    ende = false;
  }
  host = ha;
  pfad = p;
  an = true;
  laeuft = true;
  m->start();
  if (xTaskCreate(task, "durchsage", 6144, nullptr, 5, nullptr) != pdPASS) {
    an = false;
    laeuft = false;
    m->stop();
    return false;
  }
  ESP_LOGI("durchsage", "WLAN: Aufnahme laeuft (an %s)", ha.c_str());
  return true;
}

// Losgelassen: Mikrofon aus, der Task schickt den Rest und wartet auf die Antwort
inline void stop() {
  if (!an) return;
  an = false;
  if (mic != nullptr) mic->stop();
  std::lock_guard<std::mutex> g(mtx);
  ende = true;
}

}  // namespace durchsage_wlan
