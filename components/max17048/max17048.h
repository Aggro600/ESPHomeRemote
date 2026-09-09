#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace max17048 {

// Driver for the Maxim MAX17048/MAX17049 fuel gauge.
//
// NOT register-compatible with the MAX17043/MAX17044 that ESPHome's built-in
// `max17043` component targets: the VCELL register uses the full 16 bits
// (78.125 uV/LSB) instead of a 12-bit value in the upper bits (1.25 mV/LSB,
// requiring a >>4 shift). Using the stock max17043 component against a
// MAX17048 gives a voltage reading wrong by roughly a factor of 16.
class MAX17048Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void set_voltage_sensor(sensor::Sensor *s) { this->voltage_sensor_ = s; }
  void set_battery_level_sensor(sensor::Sensor *s) { this->battery_level_sensor_ = s; }

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  bool read_word_(uint8_t reg, uint16_t &value);

  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
};

}  // namespace max17048
}  // namespace esphome
