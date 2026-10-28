#pragma once
#include <functional>
// HALINK (2026-10-05): ESPHome-API ueber Bluetooth tunneln, s. __init__.py.
//
// Rahmen auf dem Link, erstes Byte = Typ:
//   HA -> Remote                         Remote -> HA
//   0x00 + Daten  API-Bytes              0x00 + Daten  API-Bytes
//   0x01          Sitzung oeffnen        0x01          Sitzung offen
//   0x02          Sitzung schliessen     0x02          Sitzung zu (Socket weg)
//   0x10 + x      Ping                   0x11 + x      Pong
//   0x12 + u32 n  Testdaten anfordern    0x13 + ...    Testdaten, 0x14 + u32 n + u32 ms = fertig
//   0x15 + ...    Testdaten (Senke)      0x17 + u32 n + u32 ms   Bericht auf 0x16
//                                        0x20 + Text   Meldung
//                                        0x21 + u8     WLAN verbunden (1) / weg (0) - bei jeder Aenderung
//                                                      und nach dem Abo, damit HA den Weg waehlen kann
//   0x30 + u32  HA hat so viele Nutzbytes (0x00/0x13) empfangen    -> Sendefenster der Remote
//   0x32 + u32  Fenstergroesse der Remote setzen
//   0x33 + 3x u16  Verbindungsintervall min/max (1,25 ms) + Latency
//   0x34 + u8   WLAN aus (0) / an (1) - Test
// HTTP-Kanal (2026-10-06, Bilder/Konfiguration von HA ueber Bluetooth):
//   0x40 + Daten  HTTP-Bytes (beide Richtungen)
//   0x41          HA: Verbindung zu HA-HTTP steht      0x43          Remote: bitte HTTP-Verbindung zu HA oeffnen
//   0x42          Ende (beide Richtungen)              0x44 + u32    Remote: so viele HTTP-Bytes empfangen (Fenster)
// Sprache (2026-10-06, HA-Assistent ohne WLAN - wie das Fernseh-Mikrofon ueber Bluetooth):
//   0x50 + u8 codec (1 = IMA-ADPCM 16 kHz)   Remote: Aufnahme beginnt
//   0x51 + i16 predictor + u8 index + ADPCM  Remote: Audio (jeder Rahmen einzeln dekodierbar)
//   0x52                                     Remote: Taste losgelassen, Ende der Aufnahme
//   0x53 + u8 typ + Text                     HA: 1 erkannt, 2 Antwort, 3 TTS-URL, 4 Fehler, 5 fertig
// Update durch den Tunnel (2026-10-06): HA-Bruecke lauscht auf 127.0.0.1:<ota_port> (HA-Host), die Remote verbindet
// per Loopback zu ihrem eigenen OTA-Server. HA -> Remote ist der grosse Strom (Firmware), daher mit Fenster.
//   0x60          HA: Verbindung zum OTA-Server oeffnen    0x61          Remote: offen
//   0x62 + Daten  beide Richtungen                          0x63          Ende (beide Richtungen)
//   0x64 + u32    Remote: so viele OTA-Bytes empfangen (Fenster)
//   0x38          Diagnose            0x39 + 12 Bytes Link (inflight, retry, congested, sendable, u32 resends,
//                                          u32 ms seit letztem Notify) + u32 tx_total, tx_acked, rx_total, to_sock,
//                                          pending, Socket, WLAN, Schleifen/s
//   0x35 + u8 mod + u8 key   HID-Taste an den verbundenen Host (Test: beide Verbindungen gleichzeitig)
//                                        0x36 + u8 hid_verbunden + u8 link   Antwort auf 0x35
//                                        0x31 + u32  Remote hat so viele Nutzbytes (0x00/0x15) empfangen
// Flusssteuerung (2026-10-05): ohne Fenster verwerfen Proxy und Remote bei Stau Pakete
// ("ATT fixed channel already congested, drop PDU") - fuer den verschluesselten API-Strom toedlich.
#include <atomic>
#include <deque>
#include "lwip/inet.h"
#include <mutex>
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include "esphome/components/microphone/microphone.h"
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/espidf_ble_keyboard/espidf_ble_keyboard.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace ha_tunnel {

class HaTunnel : public Component {
 public:
  void set_keyboard(espidf_ble_keyboard::EspidfBleKeyboard *kb) { kb_ = kb; }
  void set_port(uint16_t port) { port_ = port; }
  void set_ota_port(uint16_t port) { ota_port_ = port; }
  // HTTP-Vermittler: lauscht auf 127.0.0.1:<port>, die Remote spricht HA ueber diese Adresse an (ha_host).
  // WLAN da -> direkt zu ha_ip:ha_port, sonst durch den Tunnel (die HA-Bruecke verbindet zu HAs HTTP).
  // HA-Adresse zur Laufzeit ("IP:Port", von der Integration angekuendigt, 2026-10-19). Als Zahl in einem atomaren
  // Wert: der HTTP-Vermittler liest sie in seinem eigenen Task (http_wlan_), eine Zeichenkette koennte dabei gerade
  // ueberschrieben werden.
  void set_ha_adresse(const std::string &hp) {
    const size_t k = hp.find(':');
    if (hp.empty() || k == 0) return;
    const uint32_t ip = ipaddr_addr(hp.substr(0, k).c_str());
    if (ip == IPADDR_NONE) return;
    if (k != std::string::npos) ha_port_.store((uint32_t) atoi(hp.c_str() + k + 1));
    ha_ip_.store(ip);
  }
  // HTTP ueber das Dock (2026-10-20): Weiterleitung (dock_direkt::http_relay) und Reihenfolge.
  // weg 0 = WLAN, sonst Bluetooth, Dock nur als Ausweg; 1 = Dock zuerst, dann WLAN/Bluetooth.
  void set_http_dock(std::function<bool(int, std::vector<uint8_t> &)> &&f) { http_dock_ = std::move(f); }
  void set_http_weg(int w) { http_weg_.store((uint8_t) w); }
  // HTTP-Abruf fand keinen Weg (2026-10-25): die Remote holt dann ihr WLAN (Funkkonzept "Dock, WLAN als Ausweg").
  // Laeuft im HTTP-Task - nur Kurzes tun.
  std::function<void()> ohne_weg;
  void set_http(uint16_t port, const std::string &ha_ip, uint16_t ha_port) {
    http_port_ = port;
    const uint32_t ip = ipaddr_addr(ha_ip.c_str());
    ha_ip_.store(ip == IPADDR_NONE ? 0 : ip);
    ha_port_.store(ha_port);
  }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  bool session_open() const { return sock_ >= 0; }
  void http_task_();   // Hauptschleife des HTTP-Tasks (oeffentlich nur fuer den Task-Einstieg)
  // Sprache ueber den Link
  void set_microphone(microphone::Microphone *mic, float gain) { mic_ = mic; voice_gain_ = gain; }
  bool voice_start();   // false = kein Link oder kein Mikrofon
  void voice_stop();
  bool voice_active() const { return voice_on_; }
  void meldung(const std::string &text) { notice_(text); }   // landet ueber die Bruecke im HA-Log
  void add_on_voice_event_callback(std::function<void(int, std::string)> cb) { voice_cbs_.push_back(std::move(cb)); }
  // "Nur Bluetooth": WLAN bleibt aus, HA laeuft ueber den Link. Ist HA 3 min nicht verbunden, oeffnet der Notanker
  // der Remote (remote.yaml) das WLAN zeitweise (Wartungsfenster); der Modus selbst bleibt seit 2026-10-18 an.
  void set_ble_only(bool on);
  bool ble_only() const { return ble_only_; }
  // Display aus (2026-10-06): HA-Verbindung im Ruhetakt 120-150 ms + Latency 3 (Funk ~alle 600 ms),
  // wie die Referenz-Firmware ihren HID-Link im Leerlauf. Display an: 100-150 ms ohne Latency.
  void set_ruhe(bool r);
  // Debug-Modus der Remote (2026-10-07): Status alle 60 s ins HA-Log nur, wenn an
  void set_debug(bool d) { debug_ = d; }
  // Wartungsfenster der Remote (WLAN 3 min fuer OTA/Fernwartung): darf das WLAN auch bei "Nur Bluetooth" einschalten.
  void set_wifi_erlaubt(bool on) {
    // Beim Beginn des Fensters selbst einschalten: das wifi.enable des Wartungsfensters kam sonst schon wieder unter
    // die Raeder, bevor die 1-s-Abfrage das Fenster bemerkte (2026-10-06).
    if (on && !wifi_erlaubt_ && ble_only_ && wifi::global_wifi_component != nullptr &&
        wifi::global_wifi_component->is_disabled())
      wifi::global_wifi_component->enable();
    wifi_erlaubt_ = on;
  }
  uint32_t bytes_in() const { return bytes_in_; }
  uint32_t bytes_out() const { return bytes_out_; }

 protected:
  void handle_frame_(std::vector<uint8_t> &f);
  void open_();
  void close_(const char *why);
  bool send_frame_(uint8_t type, const uint8_t *data, size_t len);
  void notice_(const std::string &msg);
  void pump_socket_();
  void pump_test_();

  espidf_ble_keyboard::EspidfBleKeyboard *kb_{nullptr};
  uint16_t port_{6053};
  int sock_{-1};
  bool connecting_{false};
  bool was_linked_{false};
  int8_t wifi_reported_{-1};
  bool ble_only_{false};
  ESPPreferenceObject ble_only_pref_;   // "Nur Bluetooth" ueberlebt den Neustart (Template-Schalter mit Lambda tut das nicht)
  bool wifi_erlaubt_{false};
  uint32_t api_rx_ms_{0};      // letzter API-Verkehr von HA durch den Tunnel (oder Start von "Nur Bluetooth")
  uint32_t status_ms_{0};
  void idle_interval_();
  bool ruhe_{false};
  bool schlummer_{false};
  bool debug_{false};
  uint32_t session_fast_bis_{0};   // erste 20 s einer Sitzung schnell (HA gleicht alles ab)
  void ble_only_loop_();
  uint32_t loops_{0}, loops_t0_{0}, loops_per_s_{0};
  std::vector<uint8_t> to_sock_;     // vom Link, noch nicht in den Socket geschrieben
  std::vector<uint8_t> pending_;     // fertiger Datenrahmen, der beim Link noch nicht durchging
  // Steuerrahmen (alles ausser 0x00/0x13): Warteschlange, gehen vor den Daten raus und werden NIE verworfen
  // (2026-10-05: vorher gingen "Sitzung offen", Pong, Quittungen verloren, solange ein Datenrahmen wartete).
  std::deque<std::vector<uint8_t>> ctl_out_;
  bool flush_ctl_();   // true = Warteschlange leer
  uint32_t bytes_in_{0}, bytes_out_{0};
  // Flusssteuerung, Zaehler je Sitzung/Messung
  uint32_t tx_total_{0}, tx_acked_{0}, window_{4096};
  uint32_t rx_total_{0}, rx_ack_sent_{0};
  uint32_t tx_room_() const { uint32_t f = tx_total_ - tx_acked_; return f >= window_ ? 0 : window_ - f; }
  void reset_flow_() { tx_total_ = tx_acked_ = rx_total_ = rx_ack_sent_ = 0; }
  // Messung
  uint32_t test_left_{0}, test_total_{0}, test_t0_{0};
  uint32_t sink_n_{0}, sink_t0_{0}, sink_t1_{0};
  HighFrequencyLoopRequester high_freq_;
  uint32_t hf_summe_{0};   // Dauerschleife nur bei Verkehr (2026-10-07)
  uint32_t hf_bis_{0};
  // Sprache
  void voice_mic_data_(const std::vector<uint8_t> &data);
  void voice_pump_();
  microphone::Microphone *mic_{nullptr};
  float voice_gain_{6.5f};
  volatile bool voice_on_{false};
  bool voice_end_pending_{false};
  int16_t adpcm_pred_{0};
  int8_t adpcm_index_{0};
  bool nibble_hoch_{false};
  std::vector<uint8_t> voice_frame_;          // entstehender Audio-Rahmen (Mic-Task)
  // Fertige Rahmen -> Hauptschleife: Ringpuffer im PSRAM fuer 6 s Audio. Vorher 1 s im internen RAM - beim Druecken
  // laeuft der Link noch im Spartakt, der Puffer lief ueber und der ANFANG fiel raus (Nutzer 2026-10-06).
  static const size_t VOICE_FRAME_MAX = 4 + 128;
  static const size_t VOICE_RING = 384;       // 384 x 16 ms = 6,1 s
  uint8_t *voice_ring_{nullptr};
  uint16_t voice_len_[VOICE_RING]{};
  size_t voice_head_{0}, voice_tail_{0};
  uint32_t voice_frames_{0}, voice_dropped_{0};
  std::mutex voice_mtx_;
  std::vector<std::function<void(int, std::string)>> voice_cbs_;
  // HTTP-Vermittler - eigener FreeRTOS-Task: online_image ruft HTTP blockierend in der Hauptschleife ab, ein
  // Vermittler in der Hauptschleife kaeme erst nach dessen Zeitueberschreitung dran (2026-10-06).
  void http_setup_();
  void http_wlan_(int c, const std::vector<uint8_t> &vorab);
  void http_ble_(int c, const std::vector<uint8_t> &vorab);
  void ota_relay_();   // im HTTP-Task, ausgeloest durch 0x60
  bool http_send_link_(uint8_t type, const uint8_t *d, size_t n, uint32_t timeout_ms);
  uint16_t http_port_{0};
  uint16_t ota_port_{3232};
  std::function<bool(int, std::vector<uint8_t> &)> http_dock_;   // nur einmal beim Start gesetzt
  std::atomic<uint8_t> http_weg_{0};
  std::atomic<uint32_t> ha_ip_{0};      // IPv4 in Netzwerk-Reihenfolge, 0 = unbekannt
  std::atomic<uint32_t> ha_port_{8123};
  int http_listen_{-1};
  volatile uint32_t http_last_ms_{0};
  volatile bool http_fast_{false};
};

class VoiceEventTrigger : public Trigger<int, std::string> {
 public:
  explicit VoiceEventTrigger(HaTunnel *parent) {
    parent->add_on_voice_event_callback([this](int typ, std::string text) { this->trigger(typ, text); });
  }
};

}  // namespace ha_tunnel
}  // namespace esphome
