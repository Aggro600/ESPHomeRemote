/**************************************************************************/
/*!
  Author: Gustavo Ambrozio
  Based on work by: Atsushi Sasaki (https://github.com/aselectroworks/Arduino-FT6336U)
*/
/**************************************************************************/

#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/component.h"

namespace esphome::ft63x6 {

using namespace touchscreen;

static const uint8_t FT6X36_DEFAULT_THRESHOLD = 22;

class FT63X6Touchscreen final : public Touchscreen, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;

  void set_interrupt_pin(InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_threshold(uint8_t threshold) { this->threshold_ = threshold; }
  // Groesste Entfernung vom Aufsetzpunkt der letzten Beruehrung (Rohpixel), inkl. Abhebe-Position des Chips
  uint16_t weg() const { return this->max_weg_; }

 protected:
  void hard_reset_();
  void update_touches() override;

  InternalGPIOPin *interrupt_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  uint8_t threshold_{FT6X36_DEFAULT_THRESHOLD};

  uint8_t read_touch_number_();

  uint16_t read_touch_x_(uint8_t touch);
  uint16_t read_touch_y_(uint8_t touch);
  uint8_t read_touch_event_(uint8_t touch);
  uint8_t read_touch_id_(uint8_t touch);
  uint8_t read_touch_weight_(uint8_t touch);
  uint8_t read_touch_misc_(uint8_t touch);

  uint8_t read_byte_(uint8_t addr);

  // Kurze Aussetzer ueberbruecken (2026-10-11): meldet der Chip mitten im Wischen kurz "kein Finger", hielt LVGL
  // das fuer Loslassen + neues Druecken und stoppte das Scrollen. Erst nach HALTE_MS ohne Finger gilt er als weg.
  static constexpr uint32_t HALTE_MS = 45;
  bool aktiv_{false};
  bool in_luecke_{false};
  uint32_t zuletzt_ms_{0};
  // Auswertung je Beruehrung (Log beim Loslassen, nur bei Wischen)
  uint32_t start_ms_{0};
  uint16_t abfragen_{0}, punkte_{0}, luecken_{0}, fehler_{0};
  uint16_t x0_{0}, y0_{0}, lx_{0}, ly_{0};
  uint16_t max_weg_{0};
  void weg_merken_(uint16_t x, uint16_t y);
};

}  // namespace esphome::ft63x6
