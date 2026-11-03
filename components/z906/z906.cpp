#include "z906.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <driver/uart.h>
#include <cmath>
#include <cstring>

namespace esphome::z906 {

static const char *const TAG = "z906";
static const uart_port_t UART_K = UART_NUM_1;   // console
static const uart_port_t UART_V = UART_NUM_2;   // amplifier
static const uint8_t RESET_IDLE = 0x30;
static const uint8_t AUF[4] = {0x08, 0x0A, 0x0C, 0x0E};   // main, subwoofer, centre, rear up (down = +1)
static const uint8_t EINGANG_CODE[6] = {0x02, 0x05, 0x03, 0x04, 0x06, 0x07};   // inputs 1..6
static const uint8_t EFFEKT_CODE[4] = {0x14, 0x16, 0x15, 0x35};   // 3D, 2.1, 4.1, None (order of the select)
static const uint32_t STILLE_MS = 20;      // only inject when no byte has flowed for this long
static const uint32_t ANTWORT_MS = 300;    // max. duration of an AA reply

static bool effekt_moeglich(int eingang) { return eingang == 0 || eingang == 1 || eingang == 5; }   // analog only

static bool uart_einrichten(uart_port_t port, int tx, int rx) {
  uart_config_t c = {};
  c.baud_rate = 57600;
  c.data_bits = UART_DATA_8_BITS;
  c.parity = UART_PARITY_ODD;   // Z906: 8O1
  c.stop_bits = UART_STOP_BITS_1;
  c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  c.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(port, 512, 512, 0, nullptr, 0) != ESP_OK) return false;
  if (uart_param_config(port, &c) != ESP_OK) return false;
  return uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK;
}

// ---------------------------------------------------------------- Setup / task

void Z906::setup() {
  verlauf_lock_ = xSemaphoreCreateMutex();
  befehle_ = xQueueCreate(8, sizeof(Befehl));
  if (befehle_ == nullptr || !uart_einrichten(UART_K, ktx_, krx_) || !uart_einrichten(UART_V, vtx_, vrx_)) {
    ESP_LOGE(TAG, "UART setup failed");
    this->mark_failed();
    return;
  }
  // Own task, higher priority than the ESPHome loop: the forwarding must never wait for WiFi, the API or
  // logging (the old project hung there - console only started within a short time window).
  xTaskCreatePinnedToCore(Z906::task_, "z906", 4096, this, 5, nullptr, 1);
}

void Z906::task_(void *arg) { static_cast<Z906 *>(arg)->task_schleife_(); }

void Z906::task_schleife_() {
  uint8_t buf[128];
  for (;;) {
    const uint32_t jetzt = millis();
    // Console -> amplifier: forward immediately, but filtered (interference filter, see header)
    int n = uart_read_bytes(UART_K, buf, sizeof(buf), 0);
    if (n > 0) {
      letzter_verkehr_ = jetzt;
      for (int i = 0; i < n; i++) von_konsole_(buf[i]);
    }
    // Amplifier -> console: forward 1:1, read along
    n = uart_read_bytes(UART_V, buf, sizeof(buf), 0);
    if (n > 0) {
      letzter_verkehr_ = jetzt;
      for (int i = 0; i < n; i++) vom_verstaerker_(buf[i]);
    }
    if (k_pos_ >= 0 && jetzt - k_frame_t0_ > ANTWORT_MS) k_pos_ = -1;
    if (mitschnitt_) { mitschnitt_flush_(0, false); mitschnitt_flush_(1, false); }
    // Incomplete AA message: give up after ANTWORT_MS (the bytes that arrived have already been forwarded)
    if (mb_pos_ >= 0 && jetzt - mb_t0_ > ANTWORT_MS) {
      ESP_LOGW(TAG, "AA reply incomplete (%d bytes) - dropped", mb_pos_);
      mb_pos_ = -1;
      mb_eigene_ = false;
      eigene_antwort_offen_ = false;
    }
    weckfenster_pruefen_(jetzt);
    // Commands from Home Assistant: only when the line is idle, no reply is pending and the console is not waking up
    Befehl c;
    // Nach dem Einschalten (11) 4 s nichts Eigenes: die Konsole fragt dann selbst den Status ab - unser 34 genau
    // dazwischen brach ihren Start ab (2026-11-01 abends, nach dem Flashen war der Pegel unbekannt -> Abfrage)
    const bool nach_an = weck_an_ != 0 && jetzt - weck_an_ < 4000;
    if (mb_pos_ < 0 && weck_bis_ == 0 && !nach_an && (int32_t) (jetzt - antwort_bis_) >= 0 && jetzt - letzter_verkehr_ >= STILLE_MS &&
        xQueueReceive(befehle_, &c, 0) == pdTRUE) {
      uart_write_bytes(UART_V, (const char *) c.b, c.n);
      letzter_verkehr_ = jetzt;
      if (mitschnitt_) {
        char z[120];
        int k = snprintf(z, sizeof(z), "HA>V:");
        for (int i = 0; i < c.n && k < (int) sizeof(z) - 4; i++) k += snprintf(z + k, sizeof(z) - k, " %02X", c.b[i]);
        ESP_LOGI(TAG, "%s", z);
        verlauf_merken_(z);
      }
      if (c.eigene_antwort) {
        eigene_antwort_offen_ = true;
        antwort_bis_ = jetzt + ANTWORT_MS;
      }
    }
    vTaskDelay(1);
  }
}

// ---------------------------------------------------------------- Capture

void Z906::mitschnitt_byte_(int r, uint8_t b) {
  if (zeile_n_[r] > (int) sizeof(zeile_[r]) - 8) mitschnitt_flush_(r, true);
  zeile_n_[r] += snprintf(zeile_[r] + zeile_n_[r], sizeof(zeile_[r]) - zeile_n_[r], " %02X", b);
  zeile_t_[r] = millis();
}

void Z906::mitschnitt_flush_(int r, bool erzwingen) {
  if (zeile_n_[r] == 0 || (!erzwingen && millis() - zeile_t_[r] < 30)) return;
  char z[230];
  snprintf(z, sizeof(z), "%s:%s", r == 0 ? "K>V" : "V>K", zeile_[r]);   // K = console, V = amplifier
  ESP_LOGI(TAG, "%s", z);
  verlauf_merken_(z);
  zeile_n_[r] = 0;
}

// Only the first 3 minutes after start (that is what matters for the power-on problem), max. 400 lines
void Z906::verlauf_merken_(const char *text) {
  if (millis() > 180000 || verlauf_lock_ == nullptr) return;
  char z[260];
  snprintf(z, sizeof(z), "%6.2f s  %s", millis() / 1000.0f, text);
  if (xSemaphoreTake(verlauf_lock_, pdMS_TO_TICKS(20)) != pdTRUE) return;
  if (verlauf_.size() < 400) verlauf_.emplace_back(z);
  xSemaphoreGive(verlauf_lock_);
}

void Z906::notiz(const std::string &text) { verlauf_merken_(text.c_str()); }

void Z906::verlauf_ausgeben() {
  ausgabe_pos_ = 0;
  ausgabe_laeuft_ = true;   // output in portions in loop() - all at once tripped the task watchdog
}

// ---------------------------------------------------------------- Interference filter

// Bytes that occur in the protocol (github.com/nomis/logitech-z906/protocol.rst). r = 0 console->amp, 1 amp->console
bool Z906::gueltig_(int r, uint8_t b) const {
  if (b >= 0x02 && b <= 0x11) return b != 0x12 && b != 0x13;
  switch (b) {
    case 0x14: case 0x15: case 0x16: case 0x22: case 0x25: case 0x2F: case 0x30: case 0x31: case 0x33:
    case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3F: case 0xAA:
      return true;
    default:
      break;
  }
  // only from the amp: decode state 17..1F, muted/unmuted 20/21
  return r == 1 && ((b >= 0x17 && b <= 0x1F) || b == 0x20 || b == 0x21);
}

// true = Byte weitergeben. Beim Hochfahren (Verstaerker nicht an, Weckfenster, 4 s nach dem Einschalten) sperrt ein
// ungueltiges Byte die Richtung 300 ms - dort kommt nur Muell. Im laufenden Betrieb faellt nur das einzelne
// ungueltige Byte weg (2026-11-03): die 300-ms-Sperre verschluckte bei hoher Lautstaerke (mehr Stoerungen auf den
// Leitungen) echte Pegel-Schritte und Echos, die Konsole bekam keine Antwort, startete neu und der Verstaerker ging aus.
bool Z906::stoerung_(int r, uint8_t b, uint32_t jetzt) {
  const bool hochfahren = power_.load() != 1 || weckfenster_(jetzt) || (weck_an_ != 0 && jetzt - weck_an_ < 4000);
  if (!gueltig_(r, b)) {
    gefiltert_[r]++;
    if (hochfahren) stoer_bis_[r] = jetzt + 300;
    else if (jetzt - stoer_log_ms_ > 5000) {
      stoer_log_ms_ = jetzt;
      ESP_LOGW(TAG, "Stoerung im Betrieb (%s): Byte %02X verworfen", r == 0 ? "Konsole" : "Verstaerker", b);
    }
    return false;
  }
  if (hochfahren && (int32_t) (jetzt - stoer_bis_[r]) < 0) {
    gefiltert_[r]++;
    return false;
  }
  return true;
}

void Z906::von_konsole_(uint8_t b) {
  const uint32_t jetzt = millis();
  if (k_pos_ >= 0) {   // inside an AA message from the console: pass through unchanged
    k_pos_++;          // position of this byte (AA = 0, type = 1, length = 2, ..., checksum = 3 + length)
    if (k_pos_ == 2) k_len_ = b;
    uart_write_bytes(UART_V, (const char *) &b, 1);
    weitergeleitet_k_++;
    if (mitschnitt_) mitschnitt_byte_(0, b);
    if (k_pos_ >= 3 && k_pos_ >= 3 + k_len_) k_pos_ = -1;
    return;
  }
  if (!stoerung_(0, b, jetzt)) return;
  if (b == 0xAA) {
    k_pos_ = 0;
    k_frame_t0_ = jetzt;
  }
  // Wake guard (see header)
  // 34 = Status-Abfrage: im Standby beim Konsolenstart, aber auch direkt NACH dem Einschalten (11). Dann ist die
  // Konsole an - sonst oeffnete das naechste 30 ein neues Weckfenster, verwarf das Hochfahren der Lautstaerke
  // (08 ...), die Konsole bekam kein Echo und startete in einer Schleife neu (2026-11-01 abends).
  if (b == 0x36 || (b == 0x34 && power_.load() != 1)) k_standby_ = true;
  if (b == 0x30 && (k_standby_ || power_.load() == 0) && weck_bis_ == 0) {
    weck_bis_ = jetzt + 5000;
    weck_an_ = 0;
  }
  if (weckfenster_(jetzt)) {
    if (b >= 0x08 && b <= 0x0F) {
      if (mitschnitt_) verlauf_merken_("Startschutz: Pegel-Schritt verworfen");
      return;
    }
    if (b == 0x11 && weck_an_ == 0) {
      weck_an_ = jetzt;
      k_standby_ = false;
    }
  }
  uart_write_bytes(UART_V, (const char *) &b, 1);
  weitergeleitet_k_++;
  if (mitschnitt_) mitschnitt_byte_(0, b);
  // Power state after an ESP restart: a running console talks (idle time every 60 s, operation), in standby it
  // only sends 34 once at boot - so any other byte = on
  if (power_.load() < 0 && b != 0x34 && b != 0xAA) power_ = 1;
  // requests with an AA reply: we must not send anything until it is complete
  if (b == 0x34 || b == 0x31 || b == 0x25 || b == 0x2F) antwort_bis_ = jetzt + ANTWORT_MS;
}

// ---------------------------------------------------------------- Wake guard (see header)

void Z906::weckfenster_pruefen_(uint32_t jetzt) {
  if (weck_bis_ == 0) return;
  const bool an = weck_an_ != 0 && jetzt - weck_an_ > 500;   // power-on incl. the restore bytes after it is through
  if (!an && weckfenster_(jetzt)) return;
  weck_bis_ = 0;
  if (mitschnitt_) verlauf_merken_(an ? "Startschutz: Konsole an" : "Startschutz: abgelaufen ohne Einschalten");
}

// ---------------------------------------------------------------- Reading along

void Z906::vom_verstaerker_(uint8_t b) {
  if (mb_pos_ < 0) {
    const uint32_t jetzt = millis();
    if (!stoerung_(1, b, jetzt)) return;
    if (b == 0xAA) {   // start of a multi-byte message
      mb_pos_ = 0;
      mb_[mb_pos_++] = b;
      mb_t0_ = millis();
      mb_eigene_ = eigene_antwort_offen_;   // reply to our own status request: not to the console
      if (!mb_eigene_) uart_write_bytes(UART_K, (const char *) &b, 1);
      if (mitschnitt_) mitschnitt_byte_(1, b);
      return;
    }
    uart_write_bytes(UART_K, (const char *) &b, 1);
    weitergeleitet_v_++;
    if (mitschnitt_) mitschnitt_byte_(1, b);
    einzelbyte_(b);
    return;
  }
  if (mitschnitt_) mitschnitt_byte_(1, b);
  if (!mb_eigene_) {
    uart_write_bytes(UART_K, (const char *) &b, 1);
    weitergeleitet_v_++;
  } else {
    verworfen_++;
  }
  if (mb_pos_ < (int) sizeof(mb_)) mb_[mb_pos_] = b;
  mb_pos_++;
  if (mb_pos_ >= 3) {
    const int gesamt = 3 + mb_[2] + 1;   // AA, type, length, data, checksum
    if (mb_pos_ >= gesamt) {
      if (gesamt <= (int) sizeof(mb_)) antwort_fertig_();
      if (mb_eigene_) eigene_antwort_offen_ = false;
      mb_pos_ = -1;
      mb_eigene_ = false;
      antwort_bis_ = millis();   // reply complete - the line is free again
    }
  }
}

void Z906::einzelbyte_(uint8_t b) {
  if (b >= 0x08 && b <= 0x0F) {   // level up/down (echo of the amp = it really happened)
    const int kanal = (b - 0x08) / 2;
    const int alt = pegel_[kanal].load();
    if (alt >= 0) pegel_[kanal] = (int8_t) std::max(0, std::min(43, alt + ((b & 1) ? -1 : 1)));
    return;
  }
  for (int i = 0; i < 6; i++)
    if (b == EINGANG_CODE[i]) { eingang_ = (int8_t) i; return; }
  for (int e = 0; e < 4; e++)
    if (b == EFFEKT_CODE[e]) {
      const int i = eingang_.load();
      if (i >= 0) effekt_[i] = (int8_t) e;
      return;
    }
  if (b == 0x11) {   // power on (also "headphones disconnected" - the amp is on in both cases)
    power_ = 1;
    k_standby_ = false;
  } else if (b == 0x36) {
    power_ = 0;   // speakers off + save = power off (0x10 is headphones, NOT off)
  }
}

void Z906::antwort_fertig_() {
  const int laenge = mb_[2];
  uint8_t summe = 0;
  for (int i = 1; i < 3 + laenge + 1; i++) summe += mb_[i];   // type .. checksum = 0
  if (summe != 0) {
    ESP_LOGW(TAG, "AA %02X: checksum wrong", mb_[1]);
    return;
  }
  if (mb_[1] != 0x0A || laenge < 12) return;   // only status/configuration
  const uint8_t *d = mb_ + 3;
  if (d[0] <= 43) pegel_[0] = (int8_t) d[0];   // main
  if (d[1] <= 43) pegel_[3] = (int8_t) d[1];   // rear
  if (d[2] <= 43) pegel_[2] = (int8_t) d[2];   // centre
  if (d[3] <= 43) pegel_[1] = (int8_t) d[3];   // subwoofer
  if (d[4] <= 5) eingang_ = (int8_t) d[4];
  // Effects in the amp's order: inputs 4, 5, 2, 6, 1, 3. Code 0 3D, 1 2.1, 2 4.1, 3 off = order of our select
  static const int REIHENFOLGE[6] = {3, 4, 1, 5, 0, 2};
  for (int k = 0; k < 6; k++)
    if (d[6 + k] <= 3) effekt_[REIHENFOLGE[k]] = (int8_t) d[6 + k];
  ESP_LOGD(TAG, "Status: volume %u, sub %u, centre %u, rear %u, input %u", d[0], d[3], d[2], d[1], d[4] + 1);
}

// ---------------------------------------------------------------- Commands

void Z906::senden_(const uint8_t *b, size_t n, bool eigene_antwort) {
  if (befehle_ == nullptr || n == 0) return;
  Befehl c;
  c.n = (uint8_t) std::min<size_t>(n, sizeof(c.b));
  c.eigene_antwort = eigene_antwort;
  memcpy(c.b, b, c.n);
  if (xQueueSend(befehle_, &c, 0) != pdTRUE) ESP_LOGW(TAG, "command queue full");
}

void Z906::pegel_setzen(uint8_t kanal, int ziel) {
  if (kanal > 3) return;
  const int ist = pegel_[kanal].load();
  if (ist < 0) {
    ESP_LOGW(TAG, "Level unknown - reading status first, please set again afterwards");
    status_lesen();
    return;
  }
  ziel = std::max(0, std::min(43, ziel));
  uint8_t b[64];
  size_t n = 0;
  b[n++] = RESET_IDLE;
  const uint8_t code = ziel > ist ? AUF[kanal] : (uint8_t) (AUF[kanal] + 1);
  for (int s = std::abs(ziel - ist); s > 0 && n < sizeof(b); s--) b[n++] = code;
  senden_(b, n);
}

void Z906::eingang_setzen(int index) {
  if (index < 0 || index > 5) return;
  // Like the console: mute (main volume to 0), input, its effect, back up
  const int v = std::max(0, (int) pegel_[0].load());
  uint8_t b[100];
  size_t n = 0;
  b[n++] = RESET_IDLE;
  for (int s = 0; s < v; s++) b[n++] = 0x09;
  b[n++] = EINGANG_CODE[index];
  const int e = effekt_[index].load();
  b[n++] = effekt_moeglich(index) ? EFFEKT_CODE[e >= 0 ? e : 0] : EFFEKT_CODE[3];
  for (int s = 0; s < v; s++) b[n++] = 0x08;
  senden_(b, n);
}

void Z906::effekt_setzen(int index) {
  const int i = eingang_.load();
  if (index < 0 || index > 3) return;
  if (!effekt_moeglich(i)) {
    ESP_LOGW(TAG, "Effects only on inputs 1, 2 and 6 (digital: decided by the signal)");
    gemeldet_effekt_ = -2;   // republish the current value
    return;
  }
  const uint8_t b[2] = {RESET_IDLE, EFFEKT_CODE[index]};
  senden_(b, 2);
}

void Z906::status_lesen() {
  const uint8_t b = 0x34;
  senden_(&b, 1, true);
  letzte_abfrage_ = millis();
}

// ---------------------------------------------------------------- Home Assistant

void Z906::loop() {
  if (ausgabe_laeuft_ && verlauf_lock_ != nullptr && xSemaphoreTake(verlauf_lock_, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (ausgabe_pos_ == 0) ESP_LOGI(TAG, "==== Capture first 3 min after start: %u lines ====", (unsigned) verlauf_.size());
    for (int k = 0; k < 5 && ausgabe_pos_ < verlauf_.size(); k++, ausgabe_pos_++) ESP_LOGI(TAG, "%s", verlauf_[ausgabe_pos_].c_str());
    if (ausgabe_pos_ >= verlauf_.size()) {
      ausgabe_laeuft_ = false;
      ESP_LOGI(TAG, "==== End of capture (filtered as interference: console %u, amp %u bytes) ====",
               (unsigned) gefiltert_[0], (unsigned) gefiltert_[1]);
    }
    xSemaphoreGive(verlauf_lock_);
  }
  for (int k = 0; k < 4; k++) {
    const int8_t v = pegel_[k].load();
    if (pegel_n_[k] != nullptr && v >= 0 && v != gemeldet_pegel_[k]) {
      gemeldet_pegel_[k] = v;
      pegel_n_[k]->publish_state(std::round(v * 100.0f / 43.0f));   // percent like the old project
    }
  }
  const int8_t e = eingang_.load();
  if (eingang_s_ != nullptr && e >= 0 && e != gemeldet_eingang_) {
    gemeldet_eingang_ = e;
    eingang_s_->publish_state((size_t) e);
  }
  const int8_t f = e >= 0 ? (effekt_moeglich(e) ? effekt_[e].load() : (int8_t) 3) : (int8_t) -1;
  if (effekt_s_ != nullptr && f >= 0 && f != gemeldet_effekt_) {
    gemeldet_effekt_ = f;
    effekt_s_->publish_state((size_t) f);
  }
  const int8_t p = power_.load();
  if (power_b_ != nullptr && p >= 0 && p != gemeldet_power_) {
    gemeldet_power_ = p;
    power_b_->publish_state(p == 1);
  }
  // No console traffic for 75 s after the start: standby
  if (p < 0 && millis() > 75000) power_ = 0;
  // Amp on, but levels still unknown (ESP restarted while it was running): read the status itself
  if (p == 1 && pegel_[0].load() < 0 && millis() - letzte_abfrage_ > 30000) status_lesen();
  if (millis() - letzter_bericht_ > 60000) {
    letzter_bericht_ = millis();
    ESP_LOGD(TAG, "Forwarded: console->amp %u, amp->console %u bytes, own replies %u, interference filtered %u/%u",
             (unsigned) weitergeleitet_k_, (unsigned) weitergeleitet_v_, (unsigned) verworfen_, (unsigned) gefiltert_[0],
             (unsigned) gefiltert_[1]);
  }
}

void Z906::dump_config() {
  ESP_LOGCONFIG(TAG, "Logitech Z906:\n  Console: RX GPIO%d, TX GPIO%d\n  Amplifier: RX GPIO%d, TX GPIO%d\n  Capture: %s", krx_,
                ktx_, vrx_, vtx_, mitschnitt_ ? "on" : "off");
}

void Z906Level::control(float value) {   // percent -> level 0..43
  if (parent_) parent_->pegel_setzen(kanal_, (int) std::lround(std::max(0.0f, std::min(100.0f, value)) * 43.0f / 100.0f));
}
void Z906Input::control(size_t index) { if (parent_) parent_->eingang_setzen((int) index); }
void Z906Effect::control(size_t index) { if (parent_) parent_->effekt_setzen((int) index); }
void Z906StatusButton::press_action() { if (parent_) parent_->status_lesen(); }
void Z906MitschnittButton::press_action() { if (parent_) parent_->verlauf_ausgeben(); }

}  // namespace esphome::z906
