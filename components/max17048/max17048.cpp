#include "max17048.h"

#include "esphome/core/log.h"

namespace esphome {
namespace max17048 {

static const char *const TAG = "max17048";

// MAX17048/49 register map (Maxim datasheet).
static const uint8_t REG_VCELL = 0x02;
static const uint8_t REG_SOC = 0x04;
static const uint8_t REG_VERSION = 0x08;

bool MAX17048Component::read_word_(uint8_t reg, uint16_t &value) {
  uint8_t buf[2];
  if (!this->read_bytes(reg, buf, 2)) return false;
  value = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
  return true;
}

void MAX17048Component::setup() {
  uint16_t version = 0;
  if (!this->read_word_(REG_VERSION, version)) {
    ESP_LOGE(TAG, "Failed to communicate with MAX17048");
    this->mark_failed();
    return;
  }
  ESP_LOGCONFIG(TAG, "MAX17048 detected, version register: 0x%04X", version);
}

void MAX17048Component::update() {
  uint16_t vcell_raw = 0;
  if (this->read_word_(REG_VCELL, vcell_raw)) {
    // Full 16-bit value, 78.125 uV per LSB (625/8 uV) - do not reuse the
    // MAX17043 shift-by-4 / 1.25mV-per-LSB scaling here, it is wrong for
    // this chip.
    const float voltage = vcell_raw * 0.000078125f;
    if (this->voltage_sensor_ != nullptr) this->voltage_sensor_->publish_state(voltage);
  } else {
    this->status_set_warning();
    return;
  }

  uint16_t soc_raw = 0;
  if (this->read_word_(REG_SOC, soc_raw)) {
    // Upper byte = integer percent, lower byte = 1/256ths of a percent.
    float percent = soc_raw / 256.0f;
    if (percent > 100.0f) percent = 100.0f;
    if (this->battery_level_sensor_ != nullptr) this->battery_level_sensor_->publish_state(percent);
    this->status_clear_warning();
  } else {
    this->status_set_warning();
  }
}

void MAX17048Component::dump_config() {
  ESP_LOGCONFIG(TAG, "MAX17048:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with MAX17048 failed!");
  }
  LOG_SENSOR("  ", "Voltage", this->voltage_sensor_);
  LOG_SENSOR("  ", "Battery Level", this->battery_level_sensor_);
}

}  // namespace max17048
}  // namespace esphome
