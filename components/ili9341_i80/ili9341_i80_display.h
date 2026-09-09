#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/display/display_buffer.h"

#ifdef USE_ESP32

#include <vector>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_io_i80.h>

namespace esphome {
namespace ili9341_i80 {

// ESPHome's built-in `ili9xxx` display component is SPI-only (it inherits
// directly from spi::SPIDevice). This board wires its ILI9341 to the ESP32-S3
// over a genuine 8-bit parallel (Intel 8080 / "i80") bus instead - CS, DC,
// WR, RD, and 8 data lines, no SPI at all. ESPHome has no built-in i80-bus
// display driver, so this component provides one using ESP-IDF's esp_lcd i80
// APIs directly, wired up as an esphome::display::DisplayBuffer subclass so
// the existing `lvgl:` component binds to it exactly like any other display.
//
// Reuses the ILI9341 init command table already shipped with ESPHome's
// `ili9xxx` component (esphome/components/ili9xxx/ili9xxx_init.h) - that
// exact command sequence is already known-good on this class of panel; only
// the transport layer (SPI -> parallel i80) differs here.
class ILI9341I80Display : public display::DisplayBuffer {
 public:
  void set_cs_pin(InternalGPIOPin *pin) { this->cs_pin_ = pin; }
  void set_dc_pin(InternalGPIOPin *pin) { this->dc_pin_ = pin; }
  void set_wr_pin(InternalGPIOPin *pin) { this->wr_pin_ = pin; }
  void set_rd_pin(InternalGPIOPin *pin) { this->rd_pin_ = pin; }
  void set_reset_pin(InternalGPIOPin *pin) { this->reset_pin_ = pin; }
  void set_enable_pin(InternalGPIOPin *pin) { this->enable_pin_ = pin; }
  void set_data_pins(const std::array<InternalGPIOPin *, 8> &pins) { this->data_pins_ = pins; }
  void set_dimensions(int16_t width, int16_t height) {
    this->width_ = width;
    this->height_ = height;
  }
  void set_invert_colors(bool invert) { this->invert_colors_ = invert; }
  void set_mirror_x(bool mirror) { this->mirror_x_ = mirror; }
  void set_mirror_y(bool mirror) { this->mirror_y_ = mirror; }
  void set_bus_frequency(uint32_t hz) { this->pclk_hz_ = hz; }

  void setup() override;
  void update() override;
  void dump_config() override;

  // Panel-Kern in den Sleep-In-Zustand (Befehl 0x28 DISPOFF + 0x10 SLPIN).
  // Zieht den ILI9341 von ~1-2 mA auf ~uA. Aufruf vor dem Deep-Sleep; das
  // Aufwachen ist ein Reboot, setup() macht SWRESET+SLPOUT und weckt ihn.
  // Die gemeinsame Rail (GPIO38: Panel+Touch+Keypad+Accel) bleibt an, damit
  // der Tastendruck-/Bewegungs-Weckpin funktioniert.
  void enter_sleep();
  void exit_sleep();
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }
  int get_width_internal() override { return this->width_; }
  int get_height_internal() override { return this->height_; }

  // Partial-region write straight to the panel. This is the path LVGL uses
  // (packed RGB565, big-endian, no gaps) and it never touches the
  // DisplayBuffer framebuffer: only the invalidated rectangle goes over the
  // bus. See the .cpp for why bypassing the framebuffer matters.
  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset,
                      int x_pad) override;

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;
  void write_init_sequence_();
  void set_addr_window_(int x1, int y1, int x2, int y2);
  void flush_buffer_();
  // Blocks until the panel DMA has finished reading the caller's pixel buffer.
  void wait_trans_();
  static bool trans_done_cb_(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *user_ctx);

  std::array<InternalGPIOPin *, 8> data_pins_{};
  InternalGPIOPin *cs_pin_{nullptr};
  InternalGPIOPin *dc_pin_{nullptr};
  InternalGPIOPin *wr_pin_{nullptr};
  InternalGPIOPin *rd_pin_{nullptr};
  InternalGPIOPin *reset_pin_{nullptr};
  InternalGPIOPin *enable_pin_{nullptr};

  int16_t width_{240};
  int16_t height_{320};
  bool invert_colors_{false};
  bool mirror_x_{false};
  bool mirror_y_{false};

  // LovyanGFX in the reference firmware runs this bus at 40 MHz; its own
  // ghost-touch investigation (changelog 2.42/2.46) found lower clocks couple
  // less noise into the shared I2C touch bus, so this is exposed rather than
  // hard-coded.
  uint32_t pclk_hz_{20 * 1000 * 1000};

  esp_lcd_i80_bus_handle_t bus_{nullptr};
  esp_lcd_panel_io_handle_t io_handle_{nullptr};

  // Set from the esp_lcd "color transfer done" ISR callback.
  volatile bool trans_done_{true};
  // Only used by the slow path (mirroring / padded or non-565 sources), which
  // LVGL never takes.
  std::vector<uint8_t> scratch_;
};

}  // namespace ili9341_i80
}  // namespace esphome

#endif
