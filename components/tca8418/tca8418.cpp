#include "tca8418.h"

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tca8418 {

static const char *const TAG = "tca8418";

// TCA8418 register map (TI datasheet SCPS215).
static const uint8_t REG_CFG = 0x01;
static const uint8_t REG_INT_STAT = 0x02;
static const uint8_t REG_KEY_LCK_EC = 0x03;
static const uint8_t REG_KEY_EVENT_A = 0x04;  // FIFO: re-read same address per event
static const uint8_t REG_GPIO_INT_STAT1 = 0x11;  // rows 0-7
static const uint8_t REG_GPIO_INT_STAT2 = 0x12;  // cols 0-7
static const uint8_t REG_GPIO_INT_STAT3 = 0x13;  // cols 8-9
static const uint8_t REG_GPIO_DIR1 = 0x23;        // rows 0-7 direction (0 = input)
static const uint8_t REG_GPIO_INT_LVL1 = 0x26;    // rows 0-7 int level (0 = falling/low)
static const uint8_t REG_GPIO_PULL1 = 0x2C;       // rows 0-7 pull-up (0 = enabled)
static const uint8_t REG_GPIO_INT_EN1 = 0x1A;  // rows 0-7
static const uint8_t REG_GPIO_INT_EN2 = 0x1B;  // cols 0-7
static const uint8_t REG_GPIO_INT_EN3 = 0x1C;  // cols 8-9
static const uint8_t REG_KP_GPIO1 = 0x1D;      // rows 0-7 matrix-enable
static const uint8_t REG_KP_GPIO2 = 0x1E;      // cols 0-7 matrix-enable
static const uint8_t REG_KP_GPIO3 = 0x1F;      // cols 8-9 matrix-enable
static const uint8_t REG_GPI_EM1 = 0x20;       // rows 0-7 GPI event mode
static const uint8_t REG_GPI_EM2 = 0x21;       // cols 0-7 GPI event mode
static const uint8_t REG_GPI_EM3 = 0x22;       // cols 8-9 GPI event mode

static const uint8_t CFG_KE_IEN = 0x01;
static const uint8_t CFG_GPI_IEN = 0x02;

static const uint8_t INT_STAT_CLEAR_ALL = 0x1F;
static const uint8_t INT_STAT_K_GPI_MASK = 0x03;  // K_INT | GPI_INT

bool TCA8418Component::write_reg_(uint8_t reg, uint8_t value) { return this->write_byte(reg, value); }
bool TCA8418Component::read_reg_(uint8_t reg, uint8_t &value) { return this->read_byte(reg, &value); }

// Releasing the INT line takes more than writing INT_STAT. Per the TI
// datasheet and confirmed in the OpenRemote firmware (changelog 3.35: "the INT
// line stayed asserted low from the TCA8418 keypad interrupt"), the GPIO
// interrupt-status registers 0x11-0x13 must be *read* to clear the underlying
// GPI flags before INT_STAT is written - otherwise INT stays low, no further
// falling edge is ever produced, and every key after the first goes unseen.
void TCA8418Component::clear_interrupts_() {
  uint8_t ignored = 0;
  this->read_reg_(REG_GPIO_INT_STAT1, ignored);
  this->read_reg_(REG_GPIO_INT_STAT2, ignored);
  this->read_reg_(REG_GPIO_INT_STAT3, ignored);
  this->write_reg_(REG_INT_STAT, INT_STAT_CLEAR_ALL);
}

void TCA8418Component::setup() {
  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->setup();
    this->interrupt_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  }

  if (!this->configure_()) {
    ESP_LOGE(TAG, "TCA8418 configuration failed");
    this->mark_failed();
    return;
  }

  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->attach_interrupt(&TCA8418Component::gpio_intr_, this, gpio::INTERRUPT_FALLING_EDGE);
    // INT is open-drain active-low; if an event is already pending we won't see
    // a falling edge, so check the level once after attaching.
    if (!this->interrupt_pin_->digital_read()) {
      this->interrupt_pending_ = true;
    }
  }
}

void IRAM_ATTR TCA8418Component::gpio_intr_(TCA8418Component *self) { self->interrupt_pending_ = true; }

void TCA8418Component::loop() {
  // Level-driven, not purely edge-driven. The OpenRemote firmware polls the
  // INT pin's LOW level every loop rather than trusting an edge - a missed or
  // never-delivered falling edge (seen on this hardware) then still gets
  // serviced. The ISR flag is kept as a fast-path but is not the only trigger.
  if (this->interrupt_pin_ != nullptr) {
    if (!this->interrupt_pending_ && this->interrupt_pin_->digital_read()) {
      return;  // INT high (idle) and no pending flag - nothing to do
    }
  }
  this->process_events_();
}

bool TCA8418Component::configure_() {
  if (this->rows_ > 8 || this->columns_ > 10) {
    ESP_LOGE(TAG, "TCA8418 supports at most 8 rows and 10 columns (got %u x %u)", this->rows_, this->columns_);
    return false;
  }

  // Enable the requested rows/columns as scanned keypad matrix pins.
  const uint8_t kp_gpio1 = (this->rows_ >= 8) ? 0xFF : static_cast<uint8_t>((1u << this->rows_) - 1u);
  const uint8_t cols_lo = std::min<uint8_t>(this->columns_, 8);
  const uint8_t cols_hi = (this->columns_ > 8) ? static_cast<uint8_t>(this->columns_ - 8) : 0;
  const uint8_t kp_gpio2 = static_cast<uint8_t>((1u << cols_lo) - 1u);
  const uint8_t kp_gpio3 = static_cast<uint8_t>((1u << cols_hi) - 1u);

  if (!this->write_reg_(REG_KP_GPIO1, kp_gpio1)) return false;
  if (!this->write_reg_(REG_KP_GPIO2, kp_gpio2)) return false;
  if (!this->write_reg_(REG_KP_GPIO3, kp_gpio3)) return false;

  // Enable GPI event mode + interrupt for any extra individual pins outside
  // the matrix (e.g. a dedicated power button wired to a spare row pin).
  uint8_t gpi_em1 = 0, gpi_em2 = 0, gpi_em3 = 0;
  uint8_t int_en1 = 0, int_en2 = 0, int_en3 = 0;
  for (uint8_t pin : this->extra_gpi_pins_) {
    if (pin < 8) {
      gpi_em1 |= (1u << pin);
      int_en1 |= (1u << pin);
    } else if (pin < 16) {
      gpi_em2 |= (1u << (pin - 8));
      int_en2 |= (1u << (pin - 8));
    } else if (pin < 18) {
      gpi_em3 |= (1u << (pin - 16));
      int_en3 |= (1u << (pin - 16));
    } else {
      ESP_LOGW(TAG, "Ignoring out-of-range extra_gpi_pins entry %u (valid: 0-17)", pin);
    }
  }
  if (!this->write_reg_(REG_GPI_EM1, gpi_em1)) return false;
  if (!this->write_reg_(REG_GPI_EM2, gpi_em2)) return false;
  if (!this->write_reg_(REG_GPI_EM3, gpi_em3)) return false;
  if (!this->write_reg_(REG_GPIO_INT_EN1, int_en1)) return false;
  if (!this->write_reg_(REG_GPIO_INT_EN2, int_en2)) return false;
  if (!this->write_reg_(REG_GPIO_INT_EN3, int_en3)) return false;

  // Defensive: match the firmware's verified init even where these equal the
  // power-on defaults (row pins = inputs, falling-edge/low int level, pull-ups
  // enabled). Cheap, and removes any doubt about the chip's reset state.
  this->write_reg_(REG_GPIO_DIR1, 0x00);
  this->write_reg_(REG_GPIO_INT_LVL1, 0x00);
  this->write_reg_(REG_GPIO_PULL1, 0x00);

  if (!this->write_reg_(REG_CFG, CFG_KE_IEN | CFG_GPI_IEN)) return false;

  // Drain any stale FIFO entries and fully release INT before normal operation.
  uint8_t event_count = 0;
  this->read_reg_(REG_KEY_LCK_EC, event_count);
  event_count &= 0x0F;
  uint8_t dump = 0;
  for (uint8_t i = 0; i < event_count; i++) this->read_reg_(REG_KEY_EVENT_A, dump);
  this->clear_interrupts_();

  return true;
}

void TCA8418Component::process_events_() {
  this->interrupt_pending_ = false;

  uint8_t int_stat = 0;
  if (!this->read_reg_(REG_INT_STAT, int_stat)) {
    this->status_set_warning();
    return;
  }
  if ((int_stat & INT_STAT_K_GPI_MASK) == 0) {
    // No key/GPI flag but we got here, so either a spurious ISR or INT is
    // stuck low for another reason. Run the full clear either way - reading
    // 0x11-0x13 is what actually releases a stuck line.
    this->clear_interrupts_();
    return;
  }

  uint8_t event_count = 0;
  if (!this->read_reg_(REG_KEY_LCK_EC, event_count)) {
    this->status_set_warning();
    return;
  }
  event_count &= 0x0F;

  for (uint8_t i = 0; i < event_count && i < 10; i++) {
    uint8_t event = 0;
    if (!this->read_reg_(REG_KEY_EVENT_A, event)) break;
    if ((event & 0x7F) == 0) continue;
    this->handle_event_(event);
  }

  this->clear_interrupts_();
  this->status_clear_warning();
}

void TCA8418Component::handle_event_(uint8_t event) {
  const uint32_t now = millis();
  const bool pressed = (event & 0x80) != 0;
  const uint8_t keycode = event & 0x7F;
  if (keycode == 0 || keycode >= 128) return;

  if (this->debounce_ms_ > 0 && (now - this->last_event_ms_[keycode]) < this->debounce_ms_) {
    return;
  }
  this->last_event_ms_[keycode] = now;

  bool long_press = false;
  if (pressed) {
    this->pressed_state_[keycode] = true;
    this->press_start_ms_[keycode] = now;
  } else {
    if (this->pressed_state_[keycode] && this->long_press_ms_ > 0) {
      long_press = (now - this->press_start_ms_[keycode]) >= this->long_press_ms_;
    }
    this->pressed_state_[keycode] = false;
  }

  // Matrix keycodes: 1-80, TI numbering keycode = row*10 + col + 1.
  // GPI-only keycodes: 97-114 (raw pin 0-17); no row/col, reported as 0xFF.
  uint8_t row = 0xFF, col = 0xFF;
  if (keycode >= 1 && keycode <= 80) {
    row = static_cast<uint8_t>((keycode - 1) / 10);
    col = static_cast<uint8_t>((keycode - 1) % 10);
  }

  ESP_LOGD(TAG, "key event: keycode=%u pressed=%s row=%u col=%u long_press=%s", keycode,
           pressed ? "true" : "false", row, col, long_press ? "true" : "false");

  for (auto *listener : this->listeners_) listener->on_key(keycode, pressed);
  for (auto *trigger : this->key_triggers_) trigger->trigger(keycode, row, col, pressed, long_press);
}

void TCA8418Component::dump_config() {
  ESP_LOGCONFIG(TAG, "TCA8418:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with TCA8418 failed!");
  }
  ESP_LOGCONFIG(TAG, "  Matrix: %u rows x %u columns", this->rows_, this->columns_);
  if (this->interrupt_pin_ != nullptr) {
    LOG_PIN("  Interrupt Pin: ", this->interrupt_pin_);
  } else {
    ESP_LOGCONFIG(TAG, "  Interrupt Pin: none (polling mode)");
  }
  ESP_LOGCONFIG(TAG, "  Debounce: %u ms", this->debounce_ms_);
  ESP_LOGCONFIG(TAG, "  Long press threshold: %u ms", this->long_press_ms_);
  if (!this->extra_gpi_pins_.empty()) {
    std::string pins;
    for (auto p : this->extra_gpi_pins_) {
      pins += std::to_string(p);
      pins += " ";
    }
    ESP_LOGCONFIG(TAG, "  Extra GPI pins (raw index): %s", pins.c_str());
  }
}

}  // namespace tca8418
}  // namespace esphome
