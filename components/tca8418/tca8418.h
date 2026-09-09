#pragma once

#include <vector>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/gpio.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace tca8418 {

// Listener interface for binary_sensor platform (one instance per watched keycode).
class TCA8418Listener {
 public:
  virtual void on_key(uint8_t keycode, bool pressed) {}
};

// on_key automation trigger. row/col are 0xFF for GPI-only keycodes (97-114)
// that are not part of the scanned matrix.
class TCA8418KeyTrigger : public Trigger<uint8_t, uint8_t, uint8_t, bool, bool> {};

// Driver for the TI TCA8418 keypad scan IC, supporting a mix of:
//  - a scanned row x column matrix (keycodes 1-80, per TI numbering
//    keycode = row*10 + col + 1, valid up to 8 rows x 10 columns)
//  - individual "extra" GPI pins outside the matrix (keycodes 97-114,
//    keycode = 97 + raw_pin_index, raw_pin_index 0-7 = ROW0-7, 8-17 = COL0-9)
//
// This combination is required for boards (e.g. OMOTE/OpenRemote Rev5/Rev6)
// that scan most buttons as a matrix but wire one or more special buttons
// (e.g. a dedicated power button) directly to a spare row/col pin instead.
class TCA8418Component : public Component, public i2c::I2CDevice {
 public:
  void set_rows(uint8_t rows) { this->rows_ = rows; }
  void set_columns(uint8_t columns) { this->columns_ = columns; }
  void set_extra_gpi_pins(const std::vector<uint8_t> &pins) { this->extra_gpi_pins_ = pins; }
  void set_interrupt_pin(InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }
  void set_debounce_ms(uint32_t ms) { this->debounce_ms_ = ms; }
  void set_long_press_ms(uint32_t ms) { this->long_press_ms_ = ms; }

  void register_listener(TCA8418Listener *listener) { this->listeners_.push_back(listener); }
  void add_on_key_trigger(TCA8418KeyTrigger *trigger) { this->key_triggers_.push_back(trigger); }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  bool write_reg_(uint8_t reg, uint8_t value);
  bool read_reg_(uint8_t reg, uint8_t &value);
  void clear_interrupts_();
  bool configure_();
  void process_events_();
  void handle_event_(uint8_t event);
  static void IRAM_ATTR gpio_intr_(TCA8418Component *self);

  uint8_t rows_{5};
  uint8_t columns_{5};
  std::vector<uint8_t> extra_gpi_pins_{};
  InternalGPIOPin *interrupt_pin_{nullptr};
  volatile bool interrupt_pending_{false};
  uint32_t debounce_ms_{12};
  uint32_t long_press_ms_{500};

  std::vector<TCA8418Listener *> listeners_{};
  std::vector<TCA8418KeyTrigger *> key_triggers_{};

  // Indexed directly by TCA8418 keycode (1-127); small enough to keep as
  // plain fixed arrays rather than a map.
  uint32_t press_start_ms_[128] = {0};
  uint32_t last_event_ms_[128] = {0};
  bool pressed_state_[128] = {false};
};

}  // namespace tca8418
}  // namespace esphome
