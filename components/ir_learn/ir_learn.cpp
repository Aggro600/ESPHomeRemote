#include "ir_learn.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include <algorithm>

namespace esphome {
namespace ir_learn {

static const char *const TAG = "ir_learn";
// Die allermeisten Consumer-IR-Empfänger/-Protokolle (NEC, Samsung, RC5/6,
// Sony, Panasonic, ...) nutzen 38kHz - für gelernte Rohcodes gibt es keine
// andere Möglichkeit, die Trägerfrequenz zu kennen (der IR-Empfänger
// demoduliert sie schon vor der Dekodierung weg). Deckt die große Mehrheit
// ab; ein Gerät mit abweichender Frequenz (selten, z.B. manche 40kHz-
// Klimaanlagen-Fernbedienungen) müsste das ggf. später konfigurierbar machen.
static const uint32_t IR_LEARN_CARRIER_HZ = 38000;

void IRLearnComponent::setup() {
  this->slots_.resize(this->num_slots_);
  this->prefs_.reserve(this->num_slots_);
  for (uint8_t i = 0; i < this->num_slots_; i++) {
    uint32_t type = fnv1_hash("ir_learn_slot_" + std::to_string(i));
    this->prefs_.push_back(global_preferences->make_preference<IRLearnSlotData>(type));
    IRLearnSlotData loaded{};
    if (this->prefs_[i].load(&loaded)) {
      this->slots_[i] = loaded;
    }
  }
  ESP_LOGCONFIG(TAG, "IR-Lernen: %u Slots, %u geladen", (unsigned) this->num_slots_,
                (unsigned) std::count_if(this->slots_.begin(), this->slots_.end(),
                                         [](const IRLearnSlotData &s) { return s.length > 0; }));
}

void IRLearnComponent::loop() {
  if (this->pending_slot_ >= 0 && millis() > this->learn_deadline_) {
    ESP_LOGW(TAG, "Lernen Slot %d: Timeout, kein Signal empfangen", this->pending_slot_);
    this->pending_slot_ = -1;
  }
}

void IRLearnComponent::start_learn(uint8_t slot) {
  if (slot >= this->num_slots_) return;
  this->pending_slot_ = (int8_t) slot;
  this->learn_deadline_ = millis() + this->timeout_ms_;
  ESP_LOGI(TAG, "Lernen Slot %u: bereit, warte auf IR-Signal", slot);
}

void IRLearnComponent::feed_raw(const std::vector<int32_t> &raw) {
  if (this->pending_slot_ < 0 || raw.empty()) return;  // nicht im Lernmodus - Signal ignorieren
  uint8_t slot = (uint8_t) this->pending_slot_;
  this->pending_slot_ = -1;

  IRLearnSlotData data{};
  data.length = (uint8_t) std::min<size_t>(raw.size(), IR_LEARN_MAX_CODE_LEN);
  for (uint8_t i = 0; i < data.length; i++) {
    // int32_t -> int16_t: IR-Timings liegen praktisch immer weit unter
    // 32767us; kappen statt ueberzulaufen stoert die Wiedergabe nicht
    // (betrifft höchstens die letzte, ohnehin beliebig lange Schlusspause).
    int32_t v = raw[i];
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    data.data[i] = (int16_t) v;
  }
  this->slots_[slot] = data;
  this->prefs_[slot].save(&data);
  ESP_LOGI(TAG, "Lernen Slot %u: %u Impulse gespeichert", slot, (unsigned) data.length);
}

bool IRLearnComponent::is_slot_learned(uint8_t slot) const {
  return slot < this->num_slots_ && this->slots_[slot].length > 0;
}

void IRLearnComponent::replay(uint8_t slot) {
  if (!this->is_slot_learned(slot) || this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "Slot %u ist leer oder kein Transmitter konfiguriert", slot);
    return;
  }
  const auto &data = this->slots_[slot];
  std::vector<int32_t> raw;
  raw.reserve(data.length);
  for (uint8_t i = 0; i < data.length; i++) raw.push_back(data.data[i]);

  auto call = this->transmitter_->transmit();
  call.get_data()->set_data(raw);
  call.get_data()->set_carrier_frequency(IR_LEARN_CARRIER_HZ);
  call.perform();
  ESP_LOGI(TAG, "Slot %u abgespielt (%u Impulse)", slot, (unsigned) data.length);
}

void IRLearnComponent::erase(uint8_t slot) {
  if (slot >= this->num_slots_) return;
  this->slots_[slot] = IRLearnSlotData{};
  this->prefs_[slot].save(&this->slots_[slot]);
}

}  // namespace ir_learn
}  // namespace esphome
