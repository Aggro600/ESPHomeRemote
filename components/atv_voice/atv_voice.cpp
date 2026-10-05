#include "atv_voice.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esp_coexist.h"  // VERSUCH 28: BT-Vorrang waehrend des Streamings

#include <algorithm>
#include <cstring>

namespace esphome {
namespace atv_voice {

static const char *const TAG = "atv_voice";

// Kleiner RAII-Helfer, damit ein Mutex auf jedem Rueckgabepfad wieder
// freigegeben wird - send_ctl_/send_audio_frame_ haben mehrere davon.
namespace {
struct LockGuard {
  SemaphoreHandle_t sem;
  explicit LockGuard(SemaphoreHandle_t s) : sem(s) {
    if (sem != nullptr)
      xSemaphoreTake(sem, portMAX_DELAY);
  }
  ~LockGuard() {
    if (sem != nullptr)
      xSemaphoreGive(sem);
  }
};
}  // namespace

// The service instance id used when creating the attribute table. Must not
// collide with the ones the keyboard component uses for DIS/BAS/HID (0/1/2).
static const uint8_t SVC_INST_ATVV = 3;

// 128-bit UUIDs, little-endian on the wire.
// AB5E0001-5A21-4F05-BC7D-AF01F617B664 and friends.
static const uint8_t ATVV_SERVICE_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC,
                                              0x05, 0x4F, 0x21, 0x5A, 0x01, 0x00, 0x5E, 0xAB};
static const uint8_t ATVV_TX_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC,
                                         0x05, 0x4F, 0x21, 0x5A, 0x02, 0x00, 0x5E, 0xAB};
static const uint8_t ATVV_RX_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC,
                                         0x05, 0x4F, 0x21, 0x5A, 0x03, 0x00, 0x5E, 0xAB};
static const uint8_t ATVV_CTL_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC,
                                          0x05, 0x4F, 0x21, 0x5A, 0x04, 0x00, 0x5E, 0xAB};

enum {
  IDX_SVC,
  IDX_TX_CHAR,
  IDX_TX_VAL,
  IDX_RX_CHAR,
  IDX_RX_VAL,
  IDX_RX_CCC,
  IDX_CTL_CHAR,
  IDX_CTL_VAL,
  IDX_CTL_CCC,
  ATVV_IDX_NB,
};

static const uint16_t PRIMARY_SERVICE_UUID = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t CHAR_DECLARATION_UUID = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t CHAR_CLIENT_CONFIG_UUID = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t CHAR_PROP_WRITE = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t CHAR_PROP_NOTIFY = ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_READ;

static uint8_t atvv_tx_value[32] = {0};
static uint8_t atvv_rx_value[1] = {0};
static uint8_t atvv_ctl_value[1] = {0};
static uint8_t atvv_rx_ccc[2] = {0, 0};
static uint8_t atvv_ctl_ccc[2] = {0, 0};

static const esp_gatts_attr_db_t ATVV_ATTR_DB[ATVV_IDX_NB] = {
    // Service declaration
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *) &PRIMARY_SERVICE_UUID, ESP_GATT_PERM_READ, sizeof(ATVV_SERVICE_UUID),
      sizeof(ATVV_SERVICE_UUID), (uint8_t *) ATVV_SERVICE_UUID}},

    // TX: TV → remote, commands
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *) &CHAR_DECLARATION_UUID, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
      (uint8_t *) &CHAR_PROP_WRITE}},
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_128, (uint8_t *) ATVV_TX_UUID, ESP_GATT_PERM_WRITE, sizeof(atvv_tx_value), 0, atvv_tx_value}},

    // RX: remote → TV, audio frames
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *) &CHAR_DECLARATION_UUID, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
      (uint8_t *) &CHAR_PROP_NOTIFY}},
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_128, (uint8_t *) ATVV_RX_UUID, ESP_GATT_PERM_READ, sizeof(atvv_rx_value), sizeof(atvv_rx_value),
      atvv_rx_value}},
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *) &CHAR_CLIENT_CONFIG_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(atvv_rx_ccc), sizeof(atvv_rx_ccc), atvv_rx_ccc}},

    // CTL: remote → TV, control and status
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *) &CHAR_DECLARATION_UUID, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
      (uint8_t *) &CHAR_PROP_NOTIFY}},
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_128, (uint8_t *) ATVV_CTL_UUID, ESP_GATT_PERM_READ, sizeof(atvv_ctl_value),
      sizeof(atvv_ctl_value), atvv_ctl_value}},
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *) &CHAR_CLIENT_CONFIG_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(atvv_ctl_ccc), sizeof(atvv_ctl_ccc), atvv_ctl_ccc}},
};

// Wie viele Sendeeinheiten ein loop()-Durchlauf rausschieben darf. Seit
// VERSUCH 38 ist eine Einheit nur noch 20 Byte (5 ms bei 8 kHz, 2.5 ms bei
// 16 kHz), es braucht also deutlich mehr pro Durchlauf: 200/s bei 8 kHz,
// 400/s bei 16 kHz. Bei einer ESPHome-Schleife um die 60-100 Hz sind das
// 2-7 Einheiten je Durchlauf - 24 laesst Reserve und begrenzt zugleich, wie
// weit nach einer Congestion-Pause aufgeholt werden darf.
// VERSUCH 40 (2026-08-21): von 10 auf 30 erhoeht. Der Wert begrenzt auch, wie
// weit loop() nach einer Pause aufholen darf - alles darueber hinaus wird
// verworfen. Solange ein Block noch 7 Notifications kostete, war ein kleines
// Limit sinnvoll. Seit der MTU-Aushandlung (VERSUCH 39) geht ein Block in EIN
// Paket, Senden ist also billig; jetzt bremst nur noch dieses Limit. 30
// entspricht ~960 ms Aufholen und passt damit zum 1-Sekunden-Puffer.
static const uint8_t MAX_FRAMES_PER_LOOP = 30;

void AtvVoice::setup() {
  this->pcm_ = new int16_t[ATVV_PCM_BUFFER_SAMPLES];  // NOLINT(cppcoreguidelines-owning-memory)
  this->pcm_lock_ = xSemaphoreCreateMutex();
  if (this->pcm_ == nullptr || this->pcm_lock_ == nullptr) {
    ESP_LOGE(TAG, "Out of memory for the audio buffer");
    this->mark_failed();
    return;
  }
  if (this->microphone_ != nullptr) {
    this->microphone_->add_data_callback([this](const std::vector<uint8_t> &data) { this->on_mic_data_(data); });
  }
  // VERSUCH 41: eigener Task auf Kern 0 fuer den Audio-Versand. Prioritaet
  // bewusst niedriger als der BLE-Stack, aber hoeher als Leerlauf.
  this->tx_lock_ = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(&AtvVoice::audio_task_trampoline_, "atvv_audio", 4096, this, 5, &this->audio_task_, 0);
}

void AtvVoice::dump_config() {
  ESP_LOGCONFIG(TAG, "ATV Voice (Android TV Voice service):");
  ESP_LOGCONFIG(TAG, "  Codec: %s", this->codec_ == ATVV_CODEC_ADPCM_8K ? "IMA ADPCM 8 kHz" : "IMA ADPCM 16 kHz");
  ESP_LOGCONFIG(TAG, "  Sample rate: %u Hz", (unsigned) this->sample_rate_);
  ESP_LOGCONFIG(TAG, "  Protocol version reported: %u.%u", (unsigned) this->version_major_,
                (unsigned) this->version_minor_);
  ESP_LOGCONFIG(TAG, "  Notification chunk: %u bytes", (unsigned) this->max_notify_len_);
  ESP_LOGCONFIG(TAG, "  Max query length: %u ms", (unsigned) this->max_duration_ms_);
}

// ── GATT plumbing ───────────────────────────────────────────────────────────

void AtvVoice::atvv_on_gatts_reg(esp_gatt_if_t gatts_if) {
  this->gatts_if_ = gatts_if;
  esp_err_t err = esp_ble_gatts_create_attr_tab(ATVV_ATTR_DB, gatts_if, ATVV_IDX_NB, SVC_INST_ATVV);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Creating the voice service failed: %s", esp_err_to_name(err));
}

bool AtvVoice::atvv_on_attr_tab(uint8_t svc_inst, const uint16_t *handles, uint16_t count) {
  if (svc_inst != SVC_INST_ATVV)
    return false;
  if (count < ATVV_IDX_NB) {
    ESP_LOGE(TAG, "Voice service table incomplete (%u/%u handles)", (unsigned) count, (unsigned) ATVV_IDX_NB);
    return true;
  }
  memcpy(this->handles_, handles, ATVV_IDX_NB * sizeof(uint16_t));
  this->tx_handle_ = this->handles_[IDX_TX_VAL];
  this->rx_handle_ = this->handles_[IDX_RX_VAL];
  this->rx_ccc_handle_ = this->handles_[IDX_RX_CCC];
  this->ctl_handle_ = this->handles_[IDX_CTL_VAL];
  this->ctl_ccc_handle_ = this->handles_[IDX_CTL_CCC];
  this->service_created_ = true;
  esp_ble_gatts_start_service(this->handles_[IDX_SVC]);
  ESP_LOGI(TAG, "Voice service created (tx=0x%04X rx=0x%04X ctl=0x%04X)", this->tx_handle_, this->rx_handle_,
           this->ctl_handle_);
  return true;
}

void AtvVoice::atvv_on_connect(uint16_t conn_id) {
  this->conn_id_ = conn_id;
  this->connected_ = true;
  this->mtu_ = 23;
  this->rx_notify_ = false;
  this->ctl_notify_ = false;
  this->congested_ = false;
  this->state_ = ATVV_IDLE;
  this->caps_asked_ = false;
  this->caps_report_due_ = millis() + 10000;
  // VERSUCH 43 (2026-08-21): Mikrofon schon beim Verbinden starten und dann
  // durchlaufen lassen. Gemessen: der I2S-Treiber liefert erst ~1.3 s nach
  // start() die ersten Samples - wurde er erst beim Tastendruck gestartet,
  // fehlte genau diese Zeit am Anfang der Aufnahme. Der Nutzer redet aber
  // sofort los. on_mic_data_() verwirft die Daten ohnehin, solange keine
  // Anfrage laeuft, es landet also nichts im Puffer.
  if (this->microphone_ != nullptr && !this->mic_running_ && !this->mic_blocked_) {
    this->warmup_samples_remaining_ = this->sample_rate_ / 4;
    this->microphone_->start();
    this->mic_running_ = true;
  }
}

void AtvVoice::set_mic_blocked(bool blocked) {
  if (blocked == this->mic_blocked_)
    return;
  this->mic_blocked_ = blocked;
  if (blocked) {
    this->stop_streaming_(true);
    if (this->mic_running_ && this->microphone_ != nullptr) {
      this->microphone_->stop();
      this->mic_running_ = false;
    }
    ESP_LOGI(TAG, "Mikrofon gesperrt (SD-Karte) - I2S-Treiber gestoppt");
  } else {
    // Wie beim Verbinden: Treiber wieder anlegen, Einschwingzeit verwerfen.
    if (this->connected_ && this->microphone_ != nullptr && !this->mic_running_) {
      this->warmup_samples_remaining_ = this->sample_rate_ / 4;
      this->microphone_->start();
      this->mic_running_ = true;
    }
    ESP_LOGI(TAG, "Mikrofon wieder freigegeben");
  }
}

void AtvVoice::atvv_on_disconnect() {
  this->connected_ = false;
  if (this->mic_running_ && this->microphone_ != nullptr) {
    this->microphone_->stop();
    this->mic_running_ = false;
  }
  this->caps_report_due_ = 0;
  // The subscription is deliberately *not* cleared. A bonded client's CCC
  // state is supposed to survive a reconnect, and this host relies on it: it
  // writes the voice service's descriptors once, while bonding, and never
  // again — clearing them would tell it on the next read that its microphone
  // remote had gone away.
  this->stop_streaming_(false);
}

// FORK 2026-09-29b: Zustand der aktiven Verbindung parken (sie bleibt verbunden, ist aber nicht
// mehr die, an die Sprache geht) bzw. beim Zurueckwechseln wiederherstellen.
void AtvVoice::atvv_park(uint16_t conn_id) {
  this->stop_streaming_(false);
  for (auto &p : this->parked_) if (p.conn_id == conn_id) { p = {conn_id, this->mtu_, this->rx_notify_, this->ctl_notify_, this->caps_asked_}; this->connected_ = false; return; }
  this->parked_.push_back({conn_id, this->mtu_, this->rx_notify_, this->ctl_notify_, this->caps_asked_});
  this->connected_ = false;
  ESP_LOGI(TAG, "Sprachdienst: Verbindung %u geparkt (MTU %u)", (unsigned) conn_id, (unsigned) this->mtu_);
}

void AtvVoice::atvv_unpark(uint16_t conn_id) {
  for (size_t i = 0; i < this->parked_.size(); i++) {
    if (this->parked_[i].conn_id != conn_id) continue;
    const ParkedCtx c = this->parked_[i];
    this->parked_.erase(this->parked_.begin() + i);
    this->conn_id_ = c.conn_id;
    this->connected_ = true;
    this->mtu_ = c.mtu;
    this->rx_notify_ = c.rx_notify;
    this->ctl_notify_ = c.ctl_notify;
    this->caps_asked_ = c.caps_asked;
    this->congested_ = false;
    this->state_ = ATVV_IDLE;
    if (this->microphone_ != nullptr && !this->mic_running_ && !this->mic_blocked_) {
      this->warmup_samples_remaining_ = this->sample_rate_ / 4;
      this->microphone_->start();
      this->mic_running_ = true;
    }
    ESP_LOGI(TAG, "Sprachdienst: Verbindung %u wieder aktiv (MTU %u)", (unsigned) conn_id, (unsigned) c.mtu);
    return;
  }
  // Unbekannt: wie eine neue Verbindung behandeln.
  this->atvv_on_connect(conn_id);
}

void AtvVoice::atvv_forget(uint16_t conn_id) {
  for (size_t i = 0; i < this->parked_.size(); i++)
    if (this->parked_[i].conn_id == conn_id) { this->parked_.erase(this->parked_.begin() + i); return; }
}

void AtvVoice::atvv_on_mtu(uint16_t mtu) {
  this->mtu_ = mtu;
  ESP_LOGD(TAG, "MTU is %u", (unsigned) mtu);
}

void AtvVoice::atvv_on_congest(bool congested) {
  this->congested_ = congested;
  this->congested_since_ = congested ? millis() : 0;
  if (congested)
    ESP_LOGD(TAG, "Link congested — pausing audio");
}

bool AtvVoice::atvv_on_write(uint16_t handle, const uint8_t *value, uint16_t len) {
  if (handle == this->rx_ccc_handle_ && len >= 2) {
    this->rx_notify_ = (value[0] & 0x01) != 0;
    ESP_LOGD(TAG, "Audio notifications %s", this->rx_notify_ ? "enabled" : "disabled");
    return true;
  }
  if (handle == this->ctl_ccc_handle_ && len >= 2) {
    this->ctl_notify_ = (value[0] & 0x01) != 0;
    ESP_LOGD(TAG, "Control notifications %s", this->ctl_notify_ ? "enabled" : "disabled");
    return true;
  }
  if (handle != this->tx_handle_ || len == 0)
    return false;

  switch (value[0]) {
    case ATVV_TX_GET_CAPS:
      this->handle_get_caps_(value, len);
      break;
    case ATVV_TX_MIC_OPEN:
      this->handle_mic_open_(value, len);
      break;
    case ATVV_TX_MIC_CLOSE:
      ESP_LOGI(TAG, "Host closed the microphone");
      // Kam das Schliessen noch vor dem Start (Anlaufzeit), gar nicht erst streamen.
      this->host_start_pending_ = false;
      // VERSUCH 11: Sony bestaetigt ein MIC_CLOSE mit AUDIO_STOP, Grund 0x00.
      this->stop_streaming_(true, ATVV_STOP_REASON_HOST);
      break;
    default:
      ESP_LOGW(TAG, "Unknown voice command 0x%02X (%u bytes) — ignored", value[0], len);
      break;
  }
  return true;
}

// ── Protocol ────────────────────────────────────────────────────────────────

void AtvVoice::handle_get_caps_(const uint8_t *value, uint16_t len) {
  this->caps_asked_ = true;
  this->caps_ever_asked_ = true;
  uint8_t host_major = len > 1 ? value[1] : 0;
  uint8_t host_minor = len > 2 ? value[2] : 0;
  uint16_t host_codecs = len > 4 ? (uint16_t) ((value[3] << 8) | value[4]) : 0;
  ESP_LOGI(TAG, "Host asked for capabilities (host version %u.%u, codecs 0x%04X)", (unsigned) host_major,
           (unsigned) host_minor, (unsigned) host_codecs);
  // FORK 2026-10-04b: erst jetzt (Host nutzt den Sprachdienst) die groessere MTU aushandeln
  espidf_ble_keyboard::ble_mtu_client_anfordern();

  uint16_t codec = this->codec_;
  if (host_codecs != 0 && (host_codecs & codec) == 0) {
    // The host does not list our codec. Fall back to the other ADPCM rate if it
    // does list that one — the audio path can run at either rate, and a working
    // stream at the wrong-but-supported rate beats none at all.
    uint16_t other = (codec == ATVV_CODEC_ADPCM_16K) ? ATVV_CODEC_ADPCM_8K : ATVV_CODEC_ADPCM_16K;
    if (host_codecs & other) {
      ESP_LOGW(TAG, "Host does not support the configured codec — falling back to %s",
               other == ATVV_CODEC_ADPCM_8K ? "ADPCM 8 kHz" : "ADPCM 16 kHz");
      codec = other;
    } else {
      ESP_LOGW(TAG, "Host lists no codec we can produce (0x%04X) — answering with ours anyway", host_codecs);
    }
  }
  this->codec_ = codec;

  // VERSUCH 5 (2026-08-20, aus echtem BLE-Sniff via `adb bugreport` +
  // Bluetooth-HCI-Snoop-Log vom Google TV Streamer selbst): die echte
  // CAPS_RESP ist NICHT 5 oder 6 Byte, sondern **20 Byte** - jede CTL-
  // Notification wird offenbar auf die Chunk-Groesse gepolstert.
  //
  // VERSUCH 10 (2026-08-20) - die eigentliche Ursache. Byte-Vergleich aller
  // drei Fernbedienungen am selben Host:
  //   Host GET_CAPS:      0a 01 00 00 03 03
  //   Sony (geht):        0b 01 00 02 03 00 14 00 00 32 31 30 ... (Seriennr.)
  //   Google-Orig (geht): 0b 01 00 03 03 00 14 00 00
  //   wir (ging nicht):   0b 01 00 00 01 00 14 00 00 ...
  //                                ^^ ^^
  // Beide funktionierenden Remotes haben an [3] einen echten Codec-Wert
  // (Sony 0x02 = ADPCM 16 kHz) und an [4] konstant 0x03. Der Codec ist also
  // **ein einzelnes Byte an [3]**, nicht ein 16-Bit-Wert ueber [3][4]. Unser
  // bisheriges `resp[3] = codec >> 8` schrieb dort immer 0x00 - wir haben dem
  // Host also "gar kein Codec" gemeldet. Das erklaert das Gesamtbild: der
  // Handshake ist formal korrekt (20 Byte), der Host akzeptiert uns und das
  // Overlay geht auf, aber er kann unseren Audiostrom mangels Codec-Angabe
  // nicht decodieren -> nie eine Sprach-Animation, nie eine Erkennung. Und es
  // erklaert, warum die Fixes an Header/Sync/Durchsatz/Timing nichts brachten:
  // die betrafen alle Audio, das der Host ohnehin verwirft.
  // Byte [6] = 0x14 = 20 ist bei allen dreien die Chunk-Groesse.
  // Sonys ASCII-Seriennummer ab [9] fehlt bei der Google-Remote, ist also
  // nicht noetig - wir polstern mit Nullen.
  uint8_t resp[20] = {0};
  resp[0] = ATVV_CTL_CAPS_RESP;
  resp[1] = this->version_major_;
  resp[2] = this->version_minor_;
  resp[3] = (uint8_t) (codec & 0xFF);  // 0x01 = ADPCM 8k, 0x02 = ADPCM 16k
  resp[4] = 0x03;                      // bei Sony und Google-Original identisch
  resp[6] = this->max_notify_len_;
  ESP_LOGD(TAG, "GET_CAPS in : %s", format_hex_pretty(value, len).c_str());
  ESP_LOGD(TAG, "CAPS_RESP out: %s", format_hex_pretty(resp, sizeof(resp)).c_str());
  this->send_ctl_(resp, sizeof(resp));
}

void AtvVoice::handle_mic_open_(const uint8_t *value, uint16_t len) {
  // VERSUCH 8 (2026-08-20, aus dem echten Sniff unserer eigenen Verbindung):
  // seit VERSUCH 5 startet atvv_start_search() das Streaming sofort, ohne auf
  // MIC_OPEN zu warten. Der Host schickt MIC_OPEN aber trotzdem noch (kommt
  // jetzt zuverlaessig, ~70ms nachdem wir AUDIO_START gesendet haben) - und
  // dieser Handler rief bisher unconditional start_streaming_() erneut auf,
  // was ein ZWEITES AUDIO_START mitten im laufenden Stream verschickte (im
  // Sniff sichtbar: zwei "04"-Notifications 116ms auseinander). Der Host
  // resettet seinen Decoder vermutlich bei jedem AUDIO_START auf einen
  // frischen Zustand - waehrend unser Encoder einfach weiterlief. Das erklaert
  // "Overlay ja, aber nie erkannte Sprache": ab dem zweiten AUDIO_START waren
  // Sender und Empfaenger dauerhaft nicht mehr synchron. Jetzt: nur reagieren,
  // wenn wir noch NICHT streamen.
  if (this->state_ == ATVV_STREAMING) {
    ESP_LOGD(TAG, "Host opened the microphone (already streaming, ignoring duplicate AUDIO_START)");
    return;
  }
  ESP_LOGI(TAG, "Host opened the microphone — powering the mic, then streaming");
  if (len > 2)
    ESP_LOGD(TAG, "MIC_OPEN codec field 0x%02X%02X", value[1], value[2]);
  // Bei der Taste besorgt die YAML-Seite den Strom, bevor sie start_search
  // aufruft. Hier fordert der Fernseher an - der Strom ist also noch aus. Erst
  // den Ausloeser feuern, damit die Versorgung angeht, dann kurz warten: ein
  // MEMS-Mikrofon braucht nach dem Einschalten einen Moment, sonst sind die
  // ersten Silben Rauschen. Nicht blockierend, der BLE-Stack laeuft weiter.
  this->host_opened_ = true;
  this->host_open_at_ = millis();
  this->host_start_pending_ = true;
  this->host_open_pending_ = true;
}

void AtvVoice::atvv_start_search() {
  if (!this->connected_ || !this->service_created_) {
    ESP_LOGW(TAG, "No host connected — cannot start a voice query");
    return;
  }
  if (this->state_ != ATVV_IDLE) {
    ESP_LOGD(TAG, "Voice query already running");
    return;
  }
  if (this->microphone_ == nullptr) {
    ESP_LOGE(TAG, "No microphone configured");
    return;
  }

  // Start capturing before the host answers: the Assistant takes a moment to
  // open, and the first syllable is usually already spoken by then. The buffer
  // caps how much pre-roll can survive.
  this->pcm_head_ = this->pcm_tail_ = 0;
  this->pcm_overrun_ = false;
  this->encoder_.reset();
  this->frame_counter_ = 0;
  this->frames_since_sync_ = 0;
  this->level_peak_ = 0;
  this->level_sum_abs_ = 0;
  this->level_count_ = 0;
  this->clip_count_ = 0;
  // VERSUCH 35: jede Aufnahme faengt garantiert unbelegt an.
  this->congested_ = false;
  this->congested_since_ = 0;
  this->agc_gain_min_ = 999.0f;
  this->agc_gain_max_ = 0.0f;
  // VERSUCH 13: 250ms bei der konfigurierten Mic-Rate verwerfen - der
  // I2S-Einschwingvorgang (im Spektrogramm als steiler DC-Offset-Bogen
  // sichtbar) ist damit vorbei, bevor echte Samples in den Ringpuffer kommen.
  // Mikrofon laeuft seit dem Verbinden bereits (VERSUCH 43) - nur absichern.
  if (!this->mic_running_) {
    this->warmup_samples_remaining_ = this->sample_rate_ / 4;
    this->microphone_->start();
    this->mic_running_ = true;
  }

  if (!this->ctl_notify_ && !this->caps_ever_asked_) {
    // Only worth warning about when the host has never touched the service at
    // all. A bonded host subscribes once, while pairing; on later connections
    // the descriptors simply stay as they were, and notifications still go out.
    ESP_LOGW(TAG, "Host has never subscribed to the voice service since boot — if it was paired "
                  "before this firmware existed, unpair and pair the remote again");
  }

  // VERSUCH 5 (2026-08-20, aus echtem BLE-Sniff der Sony RMF-TX520E): im
  // realen Mitschnitt schickt die Remote beim Mic-Tastendruck **kein**
  // START_SEARCH (0x08) und wartet auch nicht auf ein MIC_OPEN vom Host -
  // sie schickt direkt AUDIO_START (0x04) auf CTL und streamt sofort los.
  // Das erklaert vermutlich unser Problem: wir warteten auf eine Nachricht
  // (MIC_OPEN), die der Host in diesem Ablauf nie schickt. START_SEARCH kam
  // im gesamten ~10-minuetigen Mitschnitt (mehrere erfolgreiche Sessions)
  // kein einziges Mal vor. Neuer Ablauf: HID-Taste, dann sofort streamen.
  // VERSUCH 9 (2026-08-20): Nutzer-Beobachtung an beiden echten Fernbedienungen
  // (Sony UND Google-Original) - die Mic-Taste muss zum Sprechen gehalten
  // werden, ist also push-to-talk, kein einmaliger Klick. Bisher sendeten wir
  // einen kurzen Tastendruck (Press+Release binnen ~15ms) und liefen dann
  // unabhaengig davon bis zu max_duration weiter. Wenn der Host das Ende der
  // Anfrage am Loslassen der Taste festmacht, widerspricht das unserem festen
  // 10s-Fenster. Jetzt: Taste halten, bis atvv_stop_audio() sie loslaesst.
  if (this->send_hid_key_ && this->keyboard_ != nullptr) {
    ESP_LOGD(TAG, "Holding HID key '%s' for the request", this->hid_key_action_.c_str());
    this->keyboard_->execute_action("hold:" + this->hid_key_action_);
  }
  ESP_LOGI(TAG, "Voice query requested — streaming immediately (no MIC_OPEN wait)");
  this->start_streaming_();
}

void AtvVoice::atvv_stop_audio() {
  // Taste losgelassen, waehrend eine vom Host angeforderte Aufnahme noch in der Anlaufzeit war:
  // gar nicht erst streamen, den Host-Ablauf aber sauber beenden (on_stream_end).
  if (this->host_start_pending_.exchange(false) && this->state_ == ATVV_IDLE && this->host_opened_) {
    this->host_opened_ = false;
    this->stream_end_pending_ = true;
  }
  this->stop_streaming_(true);
}

void AtvVoice::start_streaming_() {
  if (this->microphone_ == nullptr || this->mic_blocked_)
    return;
  if (!this->mic_running_) {
    this->microphone_->start();
    this->mic_running_ = true;
  }
  const auto info = this->microphone_->get_audio_stream_info();
  if (info.get_sample_rate() != this->sample_rate_ &&
      info.get_sample_rate() % std::max<uint32_t>(this->sample_rate_, 1) != 0) {
    // on_mic_data_() decimates cleanly when the mic rate is an integer
    // multiple of the codec rate (e.g. 16 kHz mic -> 8 kHz codec); only warn
    // when that is not the case, since only then does pitch actually shift.
    ESP_LOGW(TAG, "Microphone runs at %u Hz but the codec announces %u Hz — speech will be pitched",
             (unsigned) info.get_sample_rate(), (unsigned) this->sample_rate_);
  }

  // VERSUCH 11: Sonys AUDIO_START ist 20 Byte und traegt Stream-Typ, Codec und
  // eine hochzaehlende Sequenznummer: 04 03 02 08 00 ... Wir schickten bisher
  // nur das nackte Opcode-Byte - der Host erfuhr beim Stream-Start also nie,
  // womit er decodieren soll.
  // VERSUCH 28 (2026-08-20): Waehrend des Streamings Bluetooth Vorrang vor
  // WLAN geben. Der klassische ESP32 teilt sich EINE Funkeinheit zwischen
  // WLAN und BLE - die Sony-Fernbedienung hat diesen Konflikt nicht und
  // schafft deshalb 401 Notifications/s (= exakt 16 kHz ADPCM ueber 20-Byte-
  // Pakete), waehrend wir bei 16 kHz auf ~147/s einbrachen. Ohne Vorrang
  // teilt die Software-Koexistenz die Funkzeit gleichmaessig auf.
  esp_coex_preference_set(ESP_COEX_PREFER_BT);

  uint8_t msg[20] = {0};
  msg[0] = ATVV_CTL_AUDIO_START;
  // Zweites Byte = Grund des Streams. 0x03 = Taste gehalten (so macht es die
  // Sony-Fernbedienung); der Streamer startet dann den Google Assistant. Hat der
  // HOST das Mikrofon angefordert (MIC_OPEN, z.B. das Mikrofon-Symbol in SmartTube),
  // muss der Grund 0x00 sein und die Stream-ID 0: nur so speist der Streamer das
  // Audio in die anfragende App ein. Beleg am 2026-09-21 per logcat: mit 0x03 startete
  // bei MIC_OPEN der Assistant, die App bekam "leeres Ergebnis"; die BT25 (0x00) liefert
  // den Text direkt ins SmartTube-Suchfeld.
  msg[1] = this->host_opened_ ? 0x00 : ATVV_STREAM_TYPE;
  msg[2] = (uint8_t) (this->codec_ & 0xFF);
  msg[3] = this->host_opened_ ? 0x00 : ++this->audio_seq_;
  ESP_LOGD(TAG, "AUDIO_START out: %s", format_hex_pretty(msg, sizeof(msg)).c_str());
  this->send_ctl_(msg, sizeof(msg));
  this->state_ = ATVV_STREAMING;
  this->state_since_ = millis();
  this->next_frame_due_us_ = micros();
}

void AtvVoice::stop_streaming_(bool notify_host, uint8_t reason) {
  if (this->state_ == ATVV_IDLE)
    return;
  // Eine vom Fernseher angeforderte Aufnahme wurde vielleicht abgebrochen,
  // bevor die Anlaufzeit um war - dann darf der Zeitgeber nicht nachtraeglich
  // doch noch losstreamen.
  this->host_start_pending_ = false;
  if (this->host_opened_) {
    this->host_opened_ = false;
    this->stream_end_pending_ = true;
  }
  if (notify_host && this->state_ == ATVV_STREAMING) {
    // VERSUCH 11: ebenfalls 20 Byte, mit Grund in Byte [1] (Sony: 0x02 wenn die
    // Taste losgelassen wurde, 0x00 nach einem MIC_CLOSE vom Host).
    uint8_t msg[20] = {0};
    msg[0] = ATVV_CTL_AUDIO_STOP;
    msg[1] = reason;
    this->send_ctl_(msg, sizeof(msg));
  }
  // VERSUCH 9: Gegenstueck zum "hold:" beim Start - die Taste erst jetzt
  // loslassen, wenn die Anfrage tatsaechlich zu Ende ist (Nutzer-Stop,
  // MIC_CLOSE vom Host, oder max_duration), nicht schon Sekunden vorher.
  if (this->send_hid_key_ && this->keyboard_ != nullptr) {
    this->keyboard_->execute_action("release");
  }
  // VERSUCH 43: Mikrofon bewusst weiterlaufen lassen - siehe atvv_on_connect.
  if (this->level_count_ > 0) {
    const uint32_t avg = (uint32_t) (this->level_sum_abs_ / this->level_count_);
    // Full scale is 32767; below ~1% (≈330) of full scale is effectively
    // silence — a dead mic, wrong wiring, or the wrong channel selected all
    // look like this. A real voice peak should reach into the thousands.
    // Peak/avg sind der Rohpegel VOR der AGC. agc_gain_min_/max_ zeigen, in
    // welchem Bereich die automatische Regelung diese Aufnahme tatsaechlich
    // verstaerkt hat (VERSUCH 16) - anders als beim alten festen Gain sagt
    // ein einzelner Wert hier nicht mehr viel.
    ESP_LOGI(TAG, "Mic level: peak %d raw, avg %u, AGC gain %.1f-%.1fx (Deckel %.1f), clipped %u/%u samples%s",
             (int) this->level_peak_, (unsigned) avg, (double) this->agc_gain_min_,
             (double) this->agc_gain_max_, (double) this->gain_, (unsigned) this->clip_count_,
             (unsigned) this->level_count_,
             this->level_peak_ < 330
                 ? " — looks silent, check mic/wiring/channel"
                 : (this->clip_count_ * 100 > this->level_count_ ? " — CLIPPING, Deckel runter!" : ""));
  }
  // VERSUCH 28: Funkzeit wieder gleichmaessig verteilen - WLAN (API, Logs,
  // OTA) soll ausserhalb einer Sprachanfrage nicht benachteiligt sein.
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);


  ESP_LOGI(TAG, "Voice query finished (%u frames)", (unsigned) this->frame_counter_);
  this->state_ = ATVV_IDLE;
}

bool AtvVoice::send_ctl_(const uint8_t *data, uint16_t len) {
  if (!this->connected_ || this->ctl_handle_ == 0)
    return false;
  // Audio-Task und Hauptschleife senden beide - hier serialisieren.
  LockGuard tx(this->tx_lock_);
  // VERSUCH 14 (2026-08-20): send_audio_frame_() bekam schon in VERSUCH 6
  // Retries bei Congestion, send_ctl_() nie - dabei geht genau hier ein
  // verlorenes Paket am teuersten: AUDIO_START/AUDIO_STOP sind einmalige
  // Ereignisse ohne Wiederholung im Stream. Verliert der Host das AUDIO_STOP,
  // weiss er nicht, dass die Anfrage fertig ist, und wartet/gibt irgendwann
  // auf - das saehe fuer den Nutzer wie eine schlecht erkannte statt gar
  // nicht abgeschlossene Aufnahme aus.
  // VERSUCH 23 (2026-08-20): 8 Versuche reichten nicht immer - im Sniff
  // verlor eine Session ihr AUDIO_STOP trotzdem, eine neue Session startete
  // beim Host noch "auf" der alten. CTL-Nachrichten sind selten (ein paar
  // pro Anfrage), ein laengeres Warten hier kostet praktisch nichts.
  esp_err_t err = ESP_FAIL;
  for (uint8_t attempt = 0; attempt < 40; attempt++) {
    err = esp_ble_gatts_send_indicate(this->gatts_if_, this->conn_id_, this->ctl_handle_, len,
                                      const_cast<uint8_t *>(data), false);
    if (err == ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Control message 0x%02X failed after retries: %s", data[0], esp_err_to_name(err));
    return false;
  }
  return true;
}

bool AtvVoice::send_audio_frame_(const uint8_t *frame) {
  LockGuard tx(this->tx_lock_);
  // VERSUCH 26 (2026-08-20): Uebertragungsgroesse an der tatsaechlichen MTU
  // ausrichten, nicht an `chunk_size`. Das Google-Original meldet in
  // CAPS_RESP ebenfalls 20 (Byte [6]), schickt sein Audio aber in
  // 203-Byte-Notifications - die beiden Werte sind also entkoppelt.
  // `chunk_size` bleibt damit rein die Angabe im Handshake, waehrend hier
  // so viel pro Paket geht, wie die Verbindung hergibt. Bei MTU 210 ist das
  // der komplette 128-Byte-Frame in EINEM Paket statt in sieben.
  const uint16_t mtu_payload = this->mtu_ > 3 ? (uint16_t) (this->mtu_ - 3) : 20;
  uint16_t chunk = (uint16_t) ATVV_FRAME_PAYLOAD;
  if (chunk > mtu_payload)
    chunk = mtu_payload;
  for (size_t off = 0; off < ATVV_FRAME_PAYLOAD; off += chunk) {
    uint16_t len = (uint16_t) std::min((size_t) chunk, ATVV_FRAME_PAYLOAD - off);
    // VERSUCH 6 Nachbesserung (2026-08-20): seit dem Wegfall des Frame-Headers
    // gibt es keinen Resync-Punkt mehr - ein einziger verlorener Chunk
    // verschiebt/zerstoert die ADPCM-Decoder-Zustaende fuer den kompletten
    // Rest der Aeusserung (Predictor+Index laufen beim Empfaenger dann
    // dauerhaft falsch weiter). Bisher wurde bei Congestion einfach verworfen
    // - das erklaert vermutlich "Overlay ja, aber keine erkannte Sprache".
    // Jetzt: ein paar Mal kurz erneut versuchen, statt sofort aufzugeben.
    esp_err_t err = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < 8; attempt++) {
      err = esp_ble_gatts_send_indicate(this->gatts_if_, this->conn_id_, this->rx_handle_, len,
                                        const_cast<uint8_t *>(frame + off), false);
      if (err == ESP_OK)
        break;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Audio chunk dropped after retries: %s — rest of this query will not decode cleanly",
               esp_err_to_name(err));
      this->congested_ = true;
      this->congested_since_ = millis();
      return false;
    }
  }
  return true;
}

// ── Audio ───────────────────────────────────────────────────────────────────

void AtvVoice::on_mic_data_(const std::vector<uint8_t> &data) {
  if (this->state_ == ATVV_IDLE || this->pcm_ == nullptr)
    return;

  const auto info = this->microphone_->get_audio_stream_info();
  const uint8_t bits = info.get_bits_per_sample();
  const uint8_t channels = info.get_channels() == 0 ? 1 : info.get_channels();
  const size_t bytes_per_sample = bits / 8;
  if (bytes_per_sample == 0)
    return;
  const size_t frames = data.size() / (bytes_per_sample * channels);

  // VERSUCH 5 Nachbesserung (2026-08-20): der ESP32-I2S-Treiber gibt bei
  // sample_rate: 8000 am INMP441 staendig ESP_ERR_TIMEOUT - das Mikrofon
  // bleibt daher bei 16 kHz (laeuft zuverlaessig), und wir rechnen hier per
  // simpler Dezimierung auf die vom Codec angekuendigte Rate (8 kHz) runter,
  // statt die ESPHome-Kernkomponente i2s_audio anzufassen.
  const uint32_t mic_rate = info.get_sample_rate();
  const uint32_t decim = (mic_rate > this->sample_rate_ && this->sample_rate_ > 0)
                             ? (mic_rate / this->sample_rate_)
                             : 1;

  // VERSUCH 44 (2026-08-21): frueher wurde der Mutex ohne Wartezeit geholt
  // und der GESAMTE Mic-Block verworfen, wenn er gerade belegt war. Seit der
  // Audio-Task alle 16 ms zugreift, passierte das staendig: das Mikrofon
  // lieferte nur noch ~13400 statt 16000 Samples/s, also 16 % Tonverlust -
  // exakt die Luecke, die ich vorher faelschlich fuer Anlaufzeit hielt.
  // Jetzt kurz warten statt wegwerfen; der Mic-Task darf das.
  if (xSemaphoreTake(this->pcm_lock_, pdMS_TO_TICKS(10)) != pdTRUE)
    return;
  for (size_t i = 0; i < frames; i++) {
    if (this->warmup_samples_remaining_ > 0) {
      this->warmup_samples_remaining_--;
      continue;
    }
    const uint8_t *p = data.data() + i * bytes_per_sample * channels;  // first channel only
    // VERSUCH 17 (2026-08-20) war ein Irrweg und ist zurueckgenommen: die
    // Verstaerkung auf vollen 24 Bit zu rechnen klang plausibel (weniger
    // Quantisierungsrauschen), machte es aber messbar SCHLECHTER - der
    // Stoerabstand fiel von 34 dB auf 20 dB. Grund: das Kuerzen auf 16 Bit
    // wirkt beim leisen INMP441 wie ein Rauschgatter und schluckt dessen
    // Eigenrauschen; mit voller Aufloesung kommt genau dieses Rauschen mit
    // durch und wird mitverstaerkt. Die Sprache wurde 2.4x lauter, das
    // Rauschen 13x. Also bewusst zurueck zum Kuerzen VOR der Verstaerkung.
    int32_t sample;
    switch (bits) {
      case 16:
        sample = (int16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
        break;
      case 32:
        // I2S mics such as the INMP441 deliver 24 bits left-aligned in 32.
        sample = (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
                            ((uint32_t) p[3] << 24));
        sample >>= 16;
        break;
      default:
        continue;
    }

    // VERSUCH 5 Nachbesserung 2 (2026-08-20): erster Test mit Dezimierung
    // (jede zweite Sample verwerfen) brachte Assistant-Overlay, aber keine
    // erkannte Sprache - Verdacht Aliasing (kein Tiefpass vor dem Downsampling
    // auf 8 kHz). Jetzt: Boxcar-Mittelwert ueber `decim` Samples statt
    // Verwerfen - einfacher, aber wirksamer Tiefpass fuer Faktor-2-Downsampling.
    if (decim > 1) {
      // VERSUCH 27 (2026-08-20): Der bisherige 2-Punkt-Mittelwert war als
      // Anti-Aliasing-Filter viel zu schwach - bei 5 kHz nur -5.1 dB, bei
      // 6 kHz -8.3 dB. Alles zwischen 4 und 8 kHz faltete sich damit ins
      // Sprachband zurueck (5 kHz -> 3 kHz, 6 kHz -> 2 kHz) und verfaelschte
      // genau den Bereich, auf den die Spracherkennung hoert.
      // Jetzt Binomialfilter [1,3,3,1]/8 (Frequenzgang cos^3): 5 kHz -15 dB,
      // 6 kHz -25 dB, 7 kHz -42 dB. Kostet drei Additionen und einen Shift.
      const int32_t filtered = (this->aa_hist_[0] + 3 * this->aa_hist_[1] + 3 * this->aa_hist_[2] + sample) / 8;
      this->aa_hist_[0] = this->aa_hist_[1];
      this->aa_hist_[1] = this->aa_hist_[2];
      this->aa_hist_[2] = sample;
      if (++this->decim_counter_ < decim)
        continue;
      this->decim_counter_ = 0;
      sample = filtered;
    }

    // Level stats on the raw, pre-gain sample — tells us what the mic itself
    // is actually picking up, independent of gain/encoding/BLE.
    const int32_t abs_sample = sample < 0 ? -sample : sample;
    if (abs_sample > this->level_peak_)
      this->level_peak_ = abs_sample;
    this->level_sum_abs_ += (uint32_t) abs_sample;
    this->level_count_++;

    float applied_gain = this->gain_;
    if (this->agc_enabled_) {
      // VERSUCH 24 (2026-08-20): zweiter Anlauf, deutlich sanfter. Die erste
      // Fassung (VERSUCH 16) reagierte mit Attack 0.1 praktisch instantan
      // (Zeitkonstante ~1ms bei 8 kHz) - das hoerte sich als Pumpen an, weil
      // der Gain noch innerhalb eines Wortes auf einzelne laute Silben
      // reagierte. Jetzt deutlich traegere Zeitkonstanten (Attack ~6ms,
      // Release ~400ms, Gain-Nachfuehrung ~120ms) - reagiert auf die
      // Lautstaerke der ganzen Aeusserung, nicht auf einzelne Laute.
      const float abs_f = (float) abs_sample;
      if (abs_f > this->agc_envelope_) {
        this->agc_envelope_ += (abs_f - this->agc_envelope_) * 0.02f;
      } else {
        this->agc_envelope_ += (abs_f - this->agc_envelope_) * 0.0003f;
      }
      if (this->agc_envelope_ < 150.0f)
        this->agc_envelope_ = 150.0f;  // Bodenwert - sonst schiesst der Gain in Stille hoch

      // Ziel ~49% Vollausschlag (Sonys gemessener Peak lag bei 34%) - mit
      // Sicherheitsabstand nach oben, falls die Huellkurve einem ploetzlichen
      // lauten Einsatz kurz hinterherhinkt.
      const float desired_gain = std::min(this->gain_, 16000.0f / this->agc_envelope_);
      this->agc_gain_ += (desired_gain - this->agc_gain_) * 0.002f;
      if (this->agc_gain_ < 1.0f)
        this->agc_gain_ = 1.0f;
      applied_gain = this->agc_gain_;
    }
    if (applied_gain < this->agc_gain_min_)
      this->agc_gain_min_ = applied_gain;
    if (applied_gain > this->agc_gain_max_)
      this->agc_gain_max_ = applied_gain;

    sample = (int32_t) (sample * applied_gain);

    // VERSUCH 19 (2026-08-20): weiches Limit statt hartem Anschlag. Der
    // Nutzer beschrieb das Ergebnis als "halb so laut wie eine Musik-MP3" -
    // gemessen stimmt das (unser Sprache-RMS 2420, Sonys 7859). Einfach den
    // Gain zu verdreifachen wuerde die lauten Stellen abschneiden und die
    // Erkennung wieder zerstoeren (siehe VERSUCH 12). Stattdessen: bis
    // `soft_knee` bleibt alles linear, darueber wird sanft komprimiert statt
    // gekappt. So koennen leise Passagen deutlich lauter werden, ohne dass
    // Spitzen verzerren.
    if (this->soft_limit_) {
      // VERSUCH 21: 8000 (VERSUCH 20) war dem Nutzer zu stark komprimiert -
      // Schwelle wieder angehoben, weniger vom Signal wird gestaucht.
      const int32_t knee = 15000;  // ~46% FS, darunter voellig unangetastet
      const int32_t absv = sample < 0 ? -sample : sample;
      if (absv > knee) {
        // Rest des Weges bis 32767 auf ein Drittel stauchen (tanh-artig,
        // aber ohne teure Mathematik im Audio-Pfad).
        const int32_t over = absv - knee;
        const int32_t headroom = 32767 - knee;
        const int32_t compressed = knee + (int32_t) ((int64_t) headroom * over / (over + headroom));
        sample = sample < 0 ? -compressed : compressed;
      }
    }

    if (sample > 32767) {
      sample = 32767;
      this->clip_count_++;
    }
    if (sample < -32768) {
      sample = -32768;
      this->clip_count_++;
    }

    this->pcm_[this->pcm_head_] = (int16_t) sample;
    this->pcm_head_ = (this->pcm_head_ + 1) % ATVV_PCM_BUFFER_SAMPLES;
    if (this->pcm_head_ == this->pcm_tail_) {
      // Full: drop the oldest sample rather than the newest, so the stream
      // stays aligned with what is being said right now.
      this->pcm_tail_ = (this->pcm_tail_ + 1) % ATVV_PCM_BUFFER_SAMPLES;
      this->pcm_overrun_ = true;
    }
  }
  xSemaphoreGive(this->pcm_lock_);
}

size_t AtvVoice::pcm_available_() {
  if (xSemaphoreTake(this->pcm_lock_, 0) != pdTRUE)
    return 0;
  size_t avail = (this->pcm_head_ + ATVV_PCM_BUFFER_SAMPLES - this->pcm_tail_) % ATVV_PCM_BUFFER_SAMPLES;
  xSemaphoreGive(this->pcm_lock_);
  return avail;
}

bool AtvVoice::pcm_pop_frame_(int16_t *out) {
  // Kurz warten statt sofort aufgeben - sonst verliert der Sendetakt einen
  // Schritt, nur weil der Mic-Task gerade schreibt (VERSUCH 44).
  if (xSemaphoreTake(this->pcm_lock_, pdMS_TO_TICKS(5)) != pdTRUE)
    return false;
  size_t avail = (this->pcm_head_ + ATVV_PCM_BUFFER_SAMPLES - this->pcm_tail_) % ATVV_PCM_BUFFER_SAMPLES;
  if (avail < ATVV_FRAME_SAMPLES) {
    xSemaphoreGive(this->pcm_lock_);
    return false;
  }
  for (size_t i = 0; i < ATVV_FRAME_SAMPLES; i++) {
    out[i] = this->pcm_[this->pcm_tail_];
    this->pcm_tail_ = (this->pcm_tail_ + 1) % ATVV_PCM_BUFFER_SAMPLES;
  }
  xSemaphoreGive(this->pcm_lock_);
  return true;
}

void AtvVoice::loop() {
  // Whether the host talks to the voice service at all is the single most
  // useful thing to know, and it is decided in the seconds after connecting.
  if (this->caps_report_due_ != 0 && (int32_t) (millis() - this->caps_report_due_) >= 0) {
    this->caps_report_due_ = 0;
    if (this->caps_asked_) {
      ESP_LOGI(TAG, "Host uses the voice service (capabilities exchanged)");
    } else if (this->connected_ && this->caps_ever_asked_) {
      // Normal: a host runs the handshake when it bonds, not on every
      // reconnect. Nothing to do.
      ESP_LOGD(TAG, "No capability handshake on this connection — already done for this bond");
    } else if (this->connected_) {
      ESP_LOGW(TAG, "Host has never asked for voice capabilities since boot. If it was paired before "
                    "this firmware existed, unpair the remote on the streamer and pair it again.");
    }
  }

  // Vorgemerkte Trigger aus dem Bluetooth-Task hier im Hauptprogramm ausfuehren (s. atv_voice.h).
  if (this->host_open_pending_.exchange(false))
    this->host_open_trigger_.trigger();
  if (this->stream_end_pending_.exchange(false))
    this->stream_end_trigger_.trigger();
  // Vom Host angeforderte Aufnahme erst starten, wenn das Mikrofon angelaufen ist UND die
  // Verbindung auf dem schnellen Intervall laeuft (der Trigger oben weckt das Display, das fordert
  // es an). Aus dem Ruhezustand lief sie sonst noch im langsamen Stromspar-Takt: "Audio chunk
  // dropped"/"buffer overran", der Streamer bekam Bruchstuecke -> NO_SPEECH (live 2026-09-28).
  // Das Mikrofon nimmt waehrenddessen schon in den Puffer auf, der Anfang geht nicht verloren.
  if (this->host_start_pending_) {
    const uint32_t el = millis() - this->host_open_at_;
    const bool fast = this->keyboard_ == nullptr || this->keyboard_->conn_fast_ready();
    if (el >= this->host_warmup_ms_ && (fast || el >= this->host_warmup_ms_ + 1500)) {
      this->host_start_pending_ = false;
      if (this->state_ != ATVV_STREAMING) {
        if (!fast)
          ESP_LOGW(TAG, "Fast connection not confirmed after %u ms - streaming anyway", (unsigned) el);
        else
          ESP_LOGD(TAG, "Host stream starts after %u ms", (unsigned) el);
        this->start_streaming_();
      }
    }
  }

  if (this->state_ == ATVV_IDLE)
    return;

  const uint32_t now = millis();

  // Streaming
  if (now - this->state_since_ > this->max_duration_ms_) {
    ESP_LOGI(TAG, "Maximum query length reached");
    this->stop_streaming_(true);
    return;
  }
  // VERSUCH 35 (2026-08-21): "belegt" darf nicht dauerhaft haengen bleiben.
  // Das Flag wurde bei jedem fehlgeschlagenen Senden gesetzt, aber nur durch
  // ein Gegenereignis des Bluetooth-Stacks wieder geloescht - blieb das aus,
  // stieg loop() fuer immer sofort aus und es ging KEIN einziges Audiopaket
  // mehr raus ("Voice query finished (0 frames)", Mikrofon lief dabei
  // einwandfrei). Genau das war der Zustand "erkennt ploetzlich gar nichts
  // mehr". Jetzt wird nach 200 ms auf jeden Fall wieder ein Versuch gewagt.
  // VERSUCH 37 (2026-08-21): Wartezeit von 200 ms auf 25 ms. 200 ms waren als
  // reine Notbremse gedacht, wurden bei 16 kHz aber zum Normalfall: dort tritt
  // Congestion regelmaessig auf, und jedes Mal stand der Stream 200 ms still.
  // Messung: SD kam auf 219 Notif/s, HD nur auf 70 - obwohl dieselbe
  // Verbindung bei SD deutlich mehr schafft. Der Engpass war also nicht der
  // Funk, sondern diese Wartezeit. 25 ms verhindert das Dauer-Haengen
  // weiterhin, kostet aber kaum Durchsatz.
  if (this->congested_) {
    if (now - this->congested_since_ < 200)
      return;
    ESP_LOGV(TAG, "Congestion flag stuck for %ums — retrying anyway", (unsigned) (now - this->congested_since_));
    this->congested_ = false;
    this->congested_since_ = 0;
  }
  if (this->pcm_overrun_) {
    ESP_LOGW(TAG, "Audio buffer overran — the link is not keeping up");
    this->pcm_overrun_ = false;
  }

}

// VERSUCH 41 (2026-08-21): Der Audio-Versand laeuft jetzt hier, in einem
// eigenen Task auf Kern 0. Vorher steckte er in loop() - und die ESPHome-
// Hauptschleife kommt nur alle ~27 ms dran, waehrend HD alle 16 ms ein Paket
// braucht. Was dazwischen faellig wurde, ging als Aufholverlust verloren
// (gemessen 87 % statt 100 % Echtzeit). Der Task taktet auf 1 ms genau und
// laesst der Hauptschleife ihre Zeit fuer API, Logs und Sensoren.
void AtvVoice::audio_task_trampoline_(void *arg) { static_cast<AtvVoice *>(arg)->audio_task_loop_(); }

void AtvVoice::audio_task_loop_() {
  int16_t samples[ATVV_FRAME_SAMPLES];
  uint8_t frame[ATVV_FRAME_PAYLOAD];
  for (;;) {
    if (this->state_ != ATVV_STREAMING) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (this->congested_) {
      // VERSUCH 42 (2026-08-21): Wartezeit von 200 auf 20 ms. Sie ist nur die
      // Notbremse gegen ein dauerhaft haengendes Flag - normal loescht der
      // Stack es selbst per Ereignis. 200 ms waren dabei teuer: waehrend der
      // Wartezeit laeuft der Sendetakt weiter, und der Aufhol-Deckel verwirft
      // anschliessend genau die Audiodaten, die in dieser Zeit angefallen sind.
      const uint32_t held = millis() - this->congested_since_;
      if (held < 20) {
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
      this->congested_ = false;
      this->congested_since_ = 0;
    }

    const uint32_t us_per_frame = (uint32_t) (1000000ULL * ATVV_FRAME_SAMPLES / this->sample_rate_);
    if ((int32_t) (micros() - this->next_frame_due_us_) < 0) {
      vTaskDelay(1);
      continue;
    }
    // Nach einer Pause nur begrenzt aufholen, sonst wird der Puffer geflutet.
    if ((int32_t) (micros() - this->next_frame_due_us_) > (int32_t) (us_per_frame * MAX_FRAMES_PER_LOOP))
      this->next_frame_due_us_ = micros() - us_per_frame * MAX_FRAMES_PER_LOOP;

    if (!this->pcm_pop_frame_(samples)) {
      vTaskDelay(1);
      continue;
    }
    // Reiner ADPCM-Nibble-Strom, kein Header pro Block (VERSUCH 6).
    for (size_t i = 0; i < ATVV_FRAME_SAMPLES; i += 2) {
      uint8_t low = this->encoder_.encode(samples[i]);
      uint8_t high = this->encoder_.encode(samples[i + 1]);
      frame[i / 2] = (uint8_t) (low | (high << 4));
    }
    if (!this->send_audio_frame_(frame)) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    this->frame_counter_++;
    this->next_frame_due_us_ += us_per_frame;

    if (this->sync_interval_ > 0 && ++this->frames_since_sync_ >= this->sync_interval_) {
      this->frames_since_sync_ = 0;
      const uint8_t sync[3] = {ATVV_CTL_AUDIO_SYNC, (uint8_t) (this->frame_counter_ >> 8),
                               (uint8_t) (this->frame_counter_ & 0xFF)};
      this->send_ctl_(sync, sizeof(sync));
    }
  }
}

}  // namespace atv_voice
}  // namespace esphome
