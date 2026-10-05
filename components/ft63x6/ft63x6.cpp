/**************************************************************************/
/*!
  Author: Gustavo Ambrozio
  Based on work by: Atsushi Sasaki (https://github.com/aselectroworks/Arduino-FT6336U)

  ESPHomeRemote local override (2026-09-16): setup() no longer WRITES any
  configuration registers to the FT6206/FT6336 (DEVICE_MODE, THRESHHOLD,
  TOUCHRATE_ACTIVE) - it only reads the chip ID for the log line. Upstream
  ESPHome writes those three unconditionally on every boot.

  A runtime on/off toggle for this (a restore_value:true global, read via a
  stored GlobalsComponent-family pointer) was tried and reverted the same
  day: it compiled, but the very first OTA carrying it never came up -
  "esphome logs" kept showing the PREVIOUS build's compile timestamp after
  every upload, with a stale/mismatched-build crash report each time,
  matching the ESP32-S3 bootloader's automatic app-rollback-on-early-crash
  behaviour. Never root-caused (reverting fixed it immediately, no time
  spent bisecting) - suspect is the GlobalsComponent<bool> vs
  RestoringGlobalsComponent<bool> split (ESPHome declares every global's ID
  under the former in Python but actually instantiates the latter for
  restore_value:true, which only surfaced as a compile-time type mismatch
  the component's build caught - plausible there was a second, run-time-only
  aspect of that split this fix didn't cover). Simplest fix: back to the
  writes being unconditionally skipped, no live toggle. If this needs to be
  switchable again, prefer ESPHome's cv.templatable()/TemplatableValue<bool>
  path over a raw stored pointer - that is the pattern the rest of ESPHome
  actually exercises for "either a literal or a lambda referencing a global"
  config options, unlike what was tried here.

  Why: OpenRemote's own phantom-touch investigation on the same touch
  controller family (FT5x06/FT6206, shared LCD_EN power rail with the panel -
  identical hardware situation to remote-wz-nils' +VSW rail) found that EVERY
  firmware version that actively wrote power-mode/threshold registers to this
  chip showed some degree of ghost/phantom touches, while the one version
  that never wrote a single register to it (relying entirely on the chip's
  own power-on-reset defaults) had zero phantom-touch reports across its
  whole service life - including one documented case where writing the wrong
  power-mode value caused stale coordinate replay outright. Source:
  https://github.com/LORDSn1per/OpenRemote-Firmware,
  archive/remote-2.0-port/PHANTOM_TOUCH_FINDINGS.md and the "Adafruit"/
  "BuyDisplay" touch-driver changelog entries in remote/OpenRemote_1.0.ino.
  Low risk either way: the register values ESPHome wrote (threshold 22,
  device mode "normal") already match this chip family's own power-on-reset
  defaults per its datasheet, so skipping the writes should be neutral at
  worst and remove one documented source of noise at best - see
  open-remote-test-5-ui.yaml's deep-sleep-wake comment for the matching
  power-rail-settle-time fix (180ms -> 300ms) from the same investigation.
*/
/**************************************************************************/

#include "ft63x6.h"
#include "esphome/core/log.h"

// Registers
// Reference: https://focuslcds.com/content/FT6236.pdf

namespace esphome::ft63x6 {

static const uint8_t FT63X6_ADDR_TD_STATUS = 0x02;
static const uint8_t FT63X6_ADDR_TOUCH1_STATE = 0x03;
static const uint8_t FT63X6_ADDR_TOUCH1_X = 0x03;
static const uint8_t FT63X6_ADDR_TOUCH1_ID = 0x05;
static const uint8_t FT63X6_ADDR_TOUCH1_Y = 0x05;
static const uint8_t FT63X6_ADDR_TOUCH1_WEIGHT = 0x07;
static const uint8_t FT63X6_ADDR_TOUCH1_MISC = 0x08;
static const uint8_t FT63X6_ADDR_CHIP_ID = 0xA3;

static const char *const TAG = "FT63X6";

void FT63X6Touchscreen::setup() {
  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
    this->interrupt_pin_->setup();
    this->attach_interrupt_(this->interrupt_pin_, gpio::INTERRUPT_ANY_EDGE);
  }

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->hard_reset_();
  }

  // Get touch resolution
  if (this->x_raw_max_ == this->x_raw_min_) {
    this->x_raw_max_ = this->display_->get_native_width();
  }
  if (this->y_raw_max_ == this->y_raw_min_) {
    this->y_raw_max_ = this->display_->get_native_height();
  }
  uint8_t chip_id = this->read_byte_(FT63X6_ADDR_CHIP_ID);
  if (chip_id != 0) {
    ESP_LOGI(TAG, "FT6336U touch driver started chipid: %d", chip_id);
  } else {
    ESP_LOGE(TAG, "FT6336U touch driver failed to start");
  }
  // KEINE Register-Schreibvorgaenge mehr hier (DEVICE_MODE/THRESHHOLD/
  // TOUCHRATE_ACTIVE) - s. Dateikopf.
}

void FT63X6Touchscreen::hard_reset_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
  }
}

void FT63X6Touchscreen::dump_config() {
  ESP_LOGCONFIG(TAG, "FT63X6 Touchscreen:");
  LOG_I2C_DEVICE(this);
  LOG_PIN("  Interrupt Pin: ", this->interrupt_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG,
                "  X Calibration: [%d, %d]\n"
                "  Y Calibration: [%d, %d]",
                this->x_raw_min_, this->x_raw_max_, this->y_raw_min_, this->y_raw_max_);
  LOG_UPDATE_INTERVAL(this);
}

void FT63X6Touchscreen::update_touches() {
  uint16_t touch_id, x, y;

  uint8_t touches = this->read_touch_number_();
  ESP_LOGV(TAG, "Touches found: %d", touches);
  if ((touches == 0x00) || (touches == 0xff)) {
    // ESP_LOGD(TAG, "No touches detected");
    return;
  }

  for (auto point = 0; point < touches; point++) {
    if (((this->read_touch_event_(point)) & 0x01) == 0) {  // checking event flag bit 6 if it is null
      touch_id = this->read_touch_id_(point);              // id1 = 0 or 1
      x = this->read_touch_x_(point);
      y = this->read_touch_y_(point);
      if ((x == 0) && (y == 0)) {
        ESP_LOGW(TAG, "Reporting a (0,0) touch on %d", touch_id);
      }
      this->add_raw_touch_position_(touch_id, x, y, this->read_touch_weight_(point));
    }
  }
}

uint8_t FT63X6Touchscreen::read_touch_number_() { return this->read_byte_(FT63X6_ADDR_TD_STATUS) & 0x0F; }
// Touch 1 functions
uint16_t FT63X6Touchscreen::read_touch_x_(uint8_t touch) {
  uint8_t read_buf[2];
  read_buf[0] = this->read_byte_(FT63X6_ADDR_TOUCH1_X + (touch * 6));
  read_buf[1] = this->read_byte_(FT63X6_ADDR_TOUCH1_X + 1 + (touch * 6));
  return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
uint16_t FT63X6Touchscreen::read_touch_y_(uint8_t touch) {
  uint8_t read_buf[2];
  read_buf[0] = this->read_byte_(FT63X6_ADDR_TOUCH1_Y + (touch * 6));
  read_buf[1] = this->read_byte_(FT63X6_ADDR_TOUCH1_Y + 1 + (touch * 6));
  return ((read_buf[0] & 0x0f) << 8) | read_buf[1];
}
uint8_t FT63X6Touchscreen::read_touch_event_(uint8_t touch) {
  return this->read_byte_(FT63X6_ADDR_TOUCH1_X + (touch * 6)) >> 6;
}
uint8_t FT63X6Touchscreen::read_touch_id_(uint8_t touch) {
  return this->read_byte_(FT63X6_ADDR_TOUCH1_ID + (touch * 6)) >> 4;
}
uint8_t FT63X6Touchscreen::read_touch_weight_(uint8_t touch) {
  return this->read_byte_(FT63X6_ADDR_TOUCH1_WEIGHT + (touch * 6));
}
uint8_t FT63X6Touchscreen::read_touch_misc_(uint8_t touch) {
  return this->read_byte_(FT63X6_ADDR_TOUCH1_MISC + (touch * 6)) >> 4;
}

uint8_t FT63X6Touchscreen::read_byte_(uint8_t addr) {
  uint8_t byte = 0;
  this->read_byte(addr, &byte);
  return byte;
}

}  // namespace esphome::ft63x6
