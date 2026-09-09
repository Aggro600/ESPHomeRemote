#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace lis3dh {

// Nur Setup: konfiguriert den Bewegungs-Interrupt einmalig beim Boot ueber
// I2C. Der eigentliche Aufwach-Trigger laeuft danach rein per Hardware ueber
// PIN_ACC_INT (GPIO2, gegen die Referenz-Firmware verifiziert) als normaler
// `binary_sensor: platform: gpio` - kein fortlaufendes I2C-Polling noetig,
// kein eigenes Sensor-Value-Interface auf diesem Component.
class LIS3DHComponent : public Component, public i2c::I2CDevice {
 public:
  void set_threshold(uint8_t threshold) { this->threshold_ = threshold; }
  void set_duration(uint8_t duration) { this->duration_ = duration; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  uint8_t threshold_{0x10};
  uint8_t duration_{0x02};
};

}  // namespace lis3dh
}  // namespace esphome
