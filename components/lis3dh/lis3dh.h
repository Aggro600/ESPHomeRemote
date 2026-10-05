#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace lis3dh {

// Beim Boot: Grundeinstellung. Im Betrieb: configure_runtime() stellt Ruck-Erkennung
// (Bewegungs-Interrupt mit Hochpass) und Doppelklopfen ein. Der Chip wertet dabei JEDE
// Messung selbst aus und haelt ein Ereignis fest, bis poll_events() es abholt - so geht
// ein kurzer Ruck zwischen zwei Abfragen nicht verloren. arm_for_sleep() legt dieselben
// Ereignisse vor dem Tiefschlaf auf den INT1-Pin (GPIO2, ext1-Wecken).
class LIS3DHComponent : public Component, public i2c::I2CDevice {
 public:
  void set_threshold(uint8_t threshold) { this->threshold_ = threshold; }
  void set_duration(uint8_t duration) { this->duration_ = duration; }

  // Laufzeit: Rohdaten in den Ausgangsregistern (fuer "Hochheben"), Ruck + Doppelklopfen
  // gelatcht, INT1-Pin still. Schwellen in LSB (~16 mg bei +-2 g).
  void configure_runtime(uint8_t ruck_ths, uint8_t klopf_ths);
  // Ruck-Schwelle im Betrieb anpassen (nur bei Aenderung ein I2C-Zugriff).
  void set_ruck_threshold(uint8_t ths);
  // Festgehaltene Ereignisse abholen (und damit loeschen). false = Lesefehler.
  bool poll_events(bool &ruck, bool &doppel);
  // Vor dem Tiefschlaf: gewaehlte Ereignisse aktiv-LOW auf INT1 legen.
  void arm_for_sleep(bool ruck, uint8_t ruck_ths, bool klopf, uint8_t klopf_ths);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  uint8_t threshold_{0x10};
  uint8_t duration_{0x02};
  uint8_t ruck_ths_{0};
  void klopf_register_(uint8_t klopf_ths);
};

}  // namespace lis3dh
}  // namespace esphome
