#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

#include <string>
#include "esphome/core/helpers.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/components/espidf_ble_keyboard/espidf_ble_keyboard.h"

#include "ima_adpcm.h"

#include "esp_gatts_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace esphome {
namespace atv_voice {

// ── Android TV Voice service (ATVV) ─────────────────────────────────────────
//
// Google's "Voice over BLE Remote Control" service — carries microphone audio
// to the TV over its own GATT service, separate from HID.
//
//   Service  AB5E0001-5A21-4F05-BC7D-AF01F617B664
//     TX     AB5E0002-…   write   TV  → remote  (commands)
//     RX     AB5E0003-…   notify  remote → TV   (audio frames)
//     CTL    AB5E0004-…   notify  remote → TV   (control/status)
//
// Real flow (confirmed by sniffing a working Sony RMF-TX520E's traffic with
// the actual streamer, via `adb bugreport` + Bluetooth HCI snoop log):
//   1. TV writes GET_CAPS on TX after connecting/bonding.
//   2. Remote answers CAPS_RESP on CTL: 20 bytes, byte[3] is the codec as a
//      single value (0x01 = ADPCM 8k, 0x02 = ADPCM 16k), byte[6] is the
//      notification chunk size.
//   3. Mic press: remote notifies AUDIO_START on CTL — 20 bytes, opcode,
//      0x03, codec, a per-request sequence number, then zero padding. No HID
//      key, no START_SEARCH, no waiting for MIC_OPEN — the host opens its own
//      listening UI purely off this notification.
//   4. Remote streams raw ADPCM nibbles on RX, chunked to the CAPS_RESP
//      chunk size, with NO per-chunk header and NO periodic AUDIO_SYNC — one
//      continuous encoder run for the whole utterance.
//   5. Remote ends with AUDIO_STOP on CTL (20 bytes, reason in byte[1]: 0x02
//      when the button was released locally, 0x00 after the host wrote
//      MIC_CLOSE on TX first).
//
// MIC_OPEN (TX, host → remote) exists in the protocol but the real streamer
// does not send it reliably even on working turns — do not gate streaming on
// it. The opcodes below are otherwise reconstructed from public documentation
// of the spec (Nordic's ATVV module for the Smart Remote 3, vendor remote
// datasheets) — the spec itself is not public.
enum : uint8_t {
  // TX: TV → remote
  ATVV_TX_GET_CAPS = 0x0A,
  ATVV_TX_MIC_OPEN = 0x0C,
  ATVV_TX_MIC_CLOSE = 0x0D,
  // CTL: remote → TV
  ATVV_CTL_AUDIO_STOP = 0x00,
  ATVV_CTL_AUDIO_START = 0x04,
  ATVV_CTL_START_SEARCH = 0x08,
  ATVV_CTL_AUDIO_SYNC = 0x0A,
  ATVV_CTL_CAPS_RESP = 0x0B,
  ATVV_CTL_MIC_OPEN_ERROR = 0x0C,
};

// CTL-Nachrichten sind nicht
// 1 Byte lang, sondern immer auf die Chunk-Groesse (20) gepolstert, und
// AUDIO_START/AUDIO_STOP tragen ein Payload:
//   AUDIO_START: 04 03 <codec> <seq>  + Nullen   (seq zaehlt pro Anfrage hoch)
//   AUDIO_STOP:  00 <reason> 00 00    + Nullen
// Byte 0x03 taucht auch in CAPS_RESP[4] auf - offenbar eine feste Kennung des
// Stream-Typs.
static const uint8_t ATVV_STREAM_TYPE = 0x03;
static const uint8_t ATVV_STOP_REASON_LOCAL = 0x02;  // wir beenden (Taste los)
static const uint8_t ATVV_STOP_REASON_HOST = 0x00;   // Host schickte MIC_CLOSE

// Codec bitmask exchanged in GET_CAPS / CAPS_RESP.
enum : uint16_t {
  ATVV_CODEC_ADPCM_8K = 0x0001,
  ATVV_CODEC_ADPCM_16K = 0x0002,
  ATVV_CODEC_OPUS = 0x0004,
};

// One internal encode/buffer block: 256 samples -> 128 ADPCM bytes. Purely
// our own bookkeeping granularity for the ring buffer and BLE pacing - the
// wire format itself has no per-block header or boundary marker (a real RX
// capture showed a continuous nibble stream with no header and no AUDIO_SYNC
// during an entire utterance).
//
// 256 Samples je Block, 7 Notifications im Buendel, ist ein bewusster Wert.
// Ein strikter 5-ms-Takt (40 Samples, 1 Notification) wurde gemessen und war
// schlechter - 152 statt 215 Notif/s: die Buendel fuellen die
// Sendewarteschlange, sodass der Funk immer Nachschub hat, waehrend ein
// strikter Takt hoechstens ~200 Pakete/s anbietet und jede Verzoegerung
// danach endgueltig fehlt. 320 Samples (160 Byte, 50/s wie Firmware 3.56)
// verschlechterten die Erkennung ebenfalls.
static const size_t ATVV_FRAME_SAMPLES = 256;
static const size_t ATVV_FRAME_PAYLOAD = ATVV_FRAME_SAMPLES / 2;  // 128

// Quarter of a second of 16 kHz mono audio. The microphone task fills this and
// loop() drains it; on overrun the oldest audio is dropped, because a voice
// query that lags behind the speaker is worse than one with a gap.
// Von 4096 (0.5 s bei 8 kHz) auf 8192 (1 s) verdoppelt. Bei nur 28 von 31.25
// noetigen Frames/s (10 % Audioverlust) und ohne Frame-Header - also ohne
// Resync-Punkte - fehlen sonst Stuecke mitten in den Woertern. Mit mehr Puffer werden kurze Funkengpaesse ueberbrueckt statt
// verworfen; die Aufnahme laeuft dann etwas hinterher, bleibt aber
// vollstaendig. 8192 x 2 Byte = 16 KB, der Heap hat dafuer Reserve.
static const size_t ATVV_PCM_BUFFER_SAMPLES = 8192;

enum AtvVoiceState : uint8_t {
  ATVV_IDLE = 0,
  ATVV_STREAMING,
};

class AtvVoice : public Component, public espidf_ble_keyboard::AtvVoiceHook {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_keyboard(espidf_ble_keyboard::EspidfBleKeyboard *kb) { this->keyboard_ = kb; }
  void set_microphone(microphone::Microphone *mic) { this->microphone_ = mic; }
  void set_codec(uint16_t codec) { this->codec_ = codec; }
  void set_sample_rate(uint32_t rate) { this->sample_rate_ = rate; }
  void set_gain(float gain) { this->gain_ = gain; }
  void set_agc(bool enabled) { this->agc_enabled_ = enabled; }
  void set_soft_limit(bool enabled) { this->soft_limit_ = enabled; }
  /// Umschalten HD (16 kHz) / SD (8 kHz) zur Laufzeit. Das Mikrofon laeuft
  /// immer mit 16 kHz; bei 8 kHz greift die Dezimierung in on_mic_data_().
  void set_hd_mode(bool hd) {
    this->codec_ = hd ? ATVV_CODEC_ADPCM_16K : ATVV_CODEC_ADPCM_8K;
    this->sample_rate_ = hd ? 16000 : 8000;
    // Die WLAN-Abschaltung haengt hier NICHT mehr
    // dran. Gemessen: sie beschaedigt den Koexistenz-Zeitplan dauerhaft.
    //   erste Session nach Neustart : 377 Notif/s = 81 % Echtzeit
    //   nach einem WLAN-Aus/An-Zyklus:  65 Notif/s = 14 % Echtzeit
    // Danach bekommt BLE nur noch ein Paket pro Verbindungsintervall statt
    // sechs - und das bleibt bis zum naechsten Neustart so. Ein erneutes
    // esp_coex_preference_set() beim naechsten Stream-Start repariert es
    // nicht. Der ganze Zweig ist deshalb entfernt - das WLAN bleibt an.
  }
  void set_max_notify_len(uint8_t len) { this->max_notify_len_ = len; }
  void set_sync_interval(uint8_t frames) { this->sync_interval_ = frames; }
  void set_max_duration(uint32_t ms) { this->max_duration_ms_ = ms; }
  void set_caps_version(uint8_t major, uint8_t minor) {
    this->version_major_ = major;
    this->version_minor_ = minor;
  }
  void set_send_hid_key(bool send) { this->send_hid_key_ = send; }
  /// Which key accompanies the request — "voice" (Voice Command 0x00CF) is what
  /// a Google remote's Assistant button reports; "search" (AC Search 0x0221) is
  /// what some hosts listen for instead.
  void set_hid_key_action(const std::string &action) { this->hid_key_action_ = action; }

  bool is_streaming() const { return this->state_ == ATVV_STREAMING; }

  // Der Fernseher kann die Aufnahme selbst anfordern (MIC_OPEN), etwa wenn man
  // in einer App auf das Mikrofonsymbol tippt statt die Taste zu halten. Dann
  // muss die Fernbedienung ihr Mikrofon erst mit Strom versorgen - deshalb ein
  // Ausloeser nach draussen und eine kurze Anlaufzeit, bevor gestreamt wird.
  void set_host_warmup_ms(uint32_t ms) { this->host_warmup_ms_ = ms; }
  Trigger<> *get_host_open_trigger() { return &this->host_open_trigger_; }
  Trigger<> *get_stream_end_trigger() { return &this->stream_end_trigger_; }

  // ── AtvVoiceHook ──────────────────────────────────────────────────────────
  void atvv_on_gatts_reg(esp_gatt_if_t gatts_if) override;
  bool atvv_on_attr_tab(uint8_t svc_inst, const uint16_t *handles, uint16_t count) override;
  void atvv_on_connect(uint16_t conn_id) override;
  void atvv_on_disconnect() override;
  void atvv_on_mtu(uint16_t mtu) override;
  bool atvv_on_write(uint16_t handle, const uint8_t *value, uint16_t len) override;
  void atvv_on_congest(bool congested) override;
  void atvv_start_search() override;
  void atvv_stop_audio() override;

 protected:
  void handle_get_caps_(const uint8_t *value, uint16_t len);
  void handle_mic_open_(const uint8_t *value, uint16_t len);
  void start_streaming_();
  /// reason lands in byte [1] of AUDIO_STOP: 0x02 when we end the query
  /// ourselves (button released / max duration), 0x00 when the host closed the
  /// microphone first. Both values are what the Sony remote sends.
  void stop_streaming_(bool notify_host, uint8_t reason = ATVV_STOP_REASON_LOCAL);
  bool send_ctl_(const uint8_t *data, uint16_t len);
  bool send_audio_frame_(const uint8_t *frame);
  void on_mic_data_(const std::vector<uint8_t> &data);
  // Audio-Versand laeuft in einem eigenen Task auf
  // Kern 0 statt in der ESPHome-Hauptschleife. Die kommt nur alle ~27 ms dran,
  // HD braucht aber alle 16 ms ein Paket - der Rest ging als Aufholverlust
  // verloren (gemessen 87 % statt 100 %). Der Task taktet auf 1 ms genau.
  void audio_task_loop_();
  static void audio_task_trampoline_(void *arg);
  size_t pcm_available_();
  bool pcm_pop_frame_(int16_t *out);

  espidf_ble_keyboard::EspidfBleKeyboard *keyboard_{nullptr};
  microphone::Microphone *microphone_{nullptr};

  // Configuration
  uint16_t codec_{ATVV_CODEC_ADPCM_16K};
  uint32_t sample_rate_{16000};
  float gain_{1.0f};
  uint8_t max_notify_len_{20};
  uint8_t sync_interval_{8};
  uint32_t max_duration_ms_{10000};
  uint8_t version_major_{1};
  uint8_t version_minor_{0};
  bool send_hid_key_{false};
  uint32_t host_warmup_ms_{250};
  bool host_opened_{false};
  Trigger<> host_open_trigger_;
  Trigger<> stream_end_trigger_;
  std::string hid_key_action_{"voice"};

  // GATT state
  esp_gatt_if_t gatts_if_{ESP_GATT_IF_NONE};
  uint16_t conn_id_{0};
  bool connected_{false};
  uint16_t mtu_{23};
  uint16_t handles_[16]{};
  uint16_t tx_handle_{0};
  uint16_t rx_handle_{0};
  uint16_t ctl_handle_{0};
  uint16_t rx_ccc_handle_{0};
  uint16_t ctl_ccc_handle_{0};
  bool rx_notify_{false};
  bool ctl_notify_{false};
  bool service_created_{false};
  bool congested_{false};
  // Zeitpunkt, seit wann "belegt" gilt. Ohne das
  // konnte das Flag dauerhaft haengen bleiben (siehe atv_voice.cpp).
  uint32_t congested_since_{0};
  bool caps_asked_{false};
  bool caps_ever_asked_{false};
  uint32_t caps_report_due_{0};

  // Stream state
  AtvVoiceState state_{ATVV_IDLE};
  uint32_t state_since_{0};
  // In Mikrosekunden, weil eine Sendeeinheit bei 16 kHz nur
  // 2.5 ms dauert - in ganzen Millisekunden waere das nicht darstellbar und
  // der Takt wuerde wegdriften.
  uint32_t next_frame_due_us_{0};
  uint16_t frame_counter_{0};
  uint8_t audio_seq_{0};  // zaehlt pro Sprachanfrage hoch, Byte [3] von AUDIO_START
  uint8_t frames_since_sync_{0};
  ImaAdpcmEncoder encoder_;

  // PCM ring buffer, written from the microphone task
  int16_t *pcm_{nullptr};
  size_t pcm_head_{0};
  size_t pcm_tail_{0};
  SemaphoreHandle_t pcm_lock_{nullptr};
  // Serialisiert alle BLE-Notifications: der Audio-Task und die Hauptschleife
  // (Steuerpakete) wuerden sich sonst ins Gehege kommen.
  SemaphoreHandle_t tx_lock_{nullptr};
  TaskHandle_t audio_task_{nullptr};
  bool pcm_overrun_{false};
  bool mic_running_{false};
  uint8_t mic_bits_{16};
  uint8_t mic_channels_{1};
  uint32_t decim_counter_{0};
  int32_t decim_accum_{0};
  // Verlaufsspeicher fuer den Anti-Aliasing-Filter vor der
  // Dezimierung (Binomialfilter [1,3,3,1]/8).
  int32_t aa_hist_[3]{0, 0, 0};
  // Anzahl roher Mic-Samples, die nach Start noch verworfen
  // werden - ueberbrueckt den I2S/DC-Einschwingvorgang, der sonst als lauter
  // Knacks/Schwung am Anfang jeder Aufnahme landet.
  uint32_t warmup_samples_remaining_{0};

  // Raw-mic level stats (pre-gain), reset per query, logged at the end —
  // lets you tell "mic/wiring dead" (near-zero) from "mic fine, something
  // downstream is wrong" without needing the BLE/ADPCM path at all.
  int32_t level_peak_{0};
  uint64_t level_sum_abs_{0};
  uint32_t level_count_{0};
  uint32_t clip_count_{0};  // Samples, die nach dem Gain am Anschlag klebten

  // Der Rohpegel schwankte zwischen Tests um mehr
  // als das 12-fache (437 vs. 5350) - ein fester `gain`-Wert ist entweder fuer
  // laute oder fuer leise Aufnahmen falsch. Automatische Pegelregelung: ein
  // Peak-Follower (schnell hoch, langsam runter) schaetzt die aktuelle
  // Lautstaerke, der angewandte Gain wird sanft so nachgefuehrt, dass der
  // Pegel um ~50% Vollausschlag landet. `gain` in der YAML wird zur
  // Obergrenze (verhindert Rauschverstaerkung in Stille).
  bool agc_enabled_{false};
  bool soft_limit_{true};
  float agc_envelope_{200.0f};
  float agc_gain_{1.0f};
  float agc_gain_min_{999.0f};
  float agc_gain_max_{0.0f};
};

}  // namespace atv_voice
}  // namespace esphome
