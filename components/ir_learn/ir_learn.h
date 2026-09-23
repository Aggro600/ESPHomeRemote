#pragma once

#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/remote_base/remote_base.h"

namespace esphome {
namespace ir_learn {

// Fester Speicherplatz pro Slot statt variabler Länge - roher IR-Code
// (Mark/Space in Mikrosekunden) ist ein POD-Array, das direkt so in den
// Flash-Präferenzen (NVS) abgelegt werden kann. 200 Impulse decken so gut
// wie jeden Consumer-IR-Code ab (typisch 32-100 bei NEC/Samsung/Sony &co.).
static const uint16_t IR_LEARN_MAX_CODE_LEN = 200;

struct IRLearnSlotData {
  uint8_t length{0};
  int16_t data[IR_LEARN_MAX_CODE_LEN]{};
};

// Laufzeit-IR-Lernen ohne SD-Karte/Datenbank: nimmt rohe Timing-Daten vom
// `remote_receiver` entgegen (dessen `on_raw:`-Trigger ruft `feed_raw()`
// auf), speichert sie dauerhaft im Flash (NVS-Präferenzen) und kann sie
// später über den konfigurierten `remote_transmitter` erneut aussenden -
// funktioniert für JEDES IR-Protokoll, weil roh statt protokollspezifisch
// decodiert/gesendet wird (Kehrseite: kein Fehlerausgleich wie bei
// protokollspezifischer Decodierung, aber genau das braucht eine
// Lernfunktion für unbekannte Fernbedienungen).
class IRLearnComponent : public Component {
 public:
  void set_transmitter(remote_base::RemoteTransmitterBase *transmitter) { this->transmitter_ = transmitter; }
  void set_num_slots(uint8_t n) { this->num_slots_ = n; }
  void set_timeout_ms(uint32_t ms) { this->timeout_ms_ = ms; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Bewaffnet `slot` fürs nächste über feed_raw() eintreffende Rohsignal.
  // Läuft nach `timeout_ms_` automatisch wieder ab (siehe loop()).
  void start_learn(uint8_t slot);
  void cancel_learn() { this->pending_slot_ = -1; }
  bool is_learning() const { return this->pending_slot_ >= 0; }
  int8_t pending_slot() const { return this->pending_slot_; }

  // Vom `remote_receiver`-Component bei jedem dekodierten Rohsignal
  // aufzurufen (`on_raw: - lambda: 'id(...).feed_raw(x);'`). Ignoriert das
  // Signal, wenn gerade kein Lernen aktiv ist - läuft also gefahrlos auch
  // dauerhaft im Hintergrund mit.
  void feed_raw(const std::vector<int32_t> &raw);

  bool is_slot_learned(uint8_t slot) const;
  void replay(uint8_t slot);
  void erase(uint8_t slot);

 protected:
  remote_base::RemoteTransmitterBase *transmitter_{nullptr};
  uint8_t num_slots_{12};
  uint32_t timeout_ms_{10000};

  std::vector<ESPPreferenceObject> prefs_{};
  std::vector<IRLearnSlotData> slots_{};

  int8_t pending_slot_{-1};
  uint32_t learn_deadline_{0};
};

}  // namespace ir_learn
}  // namespace esphome
