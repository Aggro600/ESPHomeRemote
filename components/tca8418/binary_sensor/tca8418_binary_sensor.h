#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/tca8418/tca8418.h"

namespace esphome {
namespace tca8418 {

// One binary_sensor per watched keycode (1-80 for matrix keys, 97-114 for
// extra GPI-only keys such as a dedicated power button).
class TCA8418BinarySensor : public TCA8418Listener, public binary_sensor::BinarySensorInitiallyOff {
 public:
  explicit TCA8418BinarySensor(uint8_t keycode) : keycode_(keycode) {}

  void on_key(uint8_t keycode, bool pressed) override {
    if (keycode == this->keycode_) {
      this->publish_state(pressed);
    }
  }

 protected:
  uint8_t keycode_;
};

}  // namespace tca8418
}  // namespace esphome
