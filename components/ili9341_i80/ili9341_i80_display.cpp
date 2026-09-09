#include "ili9341_i80_display.h"

#ifdef USE_ESP32

#include <cinttypes>

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

// Reuse ESPHome's own, already-proven ILI9341 init command table instead of
// duplicating/re-deriving it - only the transport differs in this component.
// Local copies (not an #include of the real esphome/components/ili9xxx path):
// that component is SPI-only and never gets loaded by this config (no
// `display: platform: ili9xxx` entry), so ESPHome's build never copies its
// source directory into the build tree and the cross-component include
// fails ("No such file or directory") - found via the first real
// `esphome compile` run. Both headers are pure `constexpr`/`const` data
// tables with no matching .cpp, so a local copy is complete on its own; it
// also survives `ili9xxx` being deprecated upstream in favor of `mipi_spi`.
#include "ili9xxx_defines.h"
#include "ili9xxx_init.h"

namespace esphome {
namespace ili9341_i80 {

// ili9xxx_defines.h/ili9xxx_init.h declare their command/color constants
// inside esphome::ili9xxx (unqualified in the table itself, e.g.
// ILI9XXX_CASET) - pull them in so the code below can use them unqualified
// too, matching how ESPHome's own ili9xxx_display.cpp uses them. Found via
// the first real `esphome compile` run ('was not declared in this scope').
using namespace esphome::ili9xxx;  // NOLINT

static const char *const TAG = "ili9341_i80";

// RGB565, 2 bytes per pixel.
static const size_t BYTES_PER_PIXEL = 2;

void ILI9341I80Display::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ILI9341 (8-bit parallel/i80 bus)...");

  // Panel/touch power rail (Rev6 PIN_LCD_EN = GPIO38, active-low: LOW = on).
  // The firmware's lcdPowerOn() drives a clean off->on edge and then waits
  // ~300ms for the panel's internal regulators AND the FT6206 on the same
  // rail to settle before touching the bus. Skipping that settle was why the
  // backlight came up but the panel stayed blank: the ILI9341 ignores its
  // init sequence if it arrives before its power-on reset completes.
  // NOTE: pass this pin WITHOUT `inverted:` in YAML - the active-low
  // semantics live here (digital_write(false) = LOW = panel on).
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->pin_mode(gpio::FLAG_OUTPUT);
    this->enable_pin_->digital_write(true);   // inactive (HIGH) - rail off
    delay(50);
    this->enable_pin_->digital_write(false);  // active (LOW) - rail on
    // 300ms matches the firmware's TOUCH_POWER_ON_SETTLE_MS. This same rail
    // also feeds the TCA8418 keypad and LIS3DH on the shared I2C bus, so it
    // must be settled before those components set up (they run later, at
    // setup_priority::DATA, vs this display at HARDWARE).
    delay(300);
  }
  if (this->rd_pin_ != nullptr) {
    // The i80 bus peripheral only drives CS/DC/WR/data; RD is a plain static
    // pin here since this driver never reads back from the panel.
    this->rd_pin_->pin_mode(gpio::FLAG_OUTPUT);
    this->rd_pin_->digital_write(true);  // idle high (inactive)
  }
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->pin_mode(gpio::FLAG_OUTPUT);
    this->reset_pin_->digital_write(true);
    delay(5);
    this->reset_pin_->digital_write(false);
    delay(20);
    this->reset_pin_->digital_write(true);
    delay(150);
  }

  esp_lcd_i80_bus_config_t bus_config{};
  bus_config.dc_gpio_num = this->dc_pin_->get_pin();
  bus_config.wr_gpio_num = this->wr_pin_->get_pin();
  bus_config.clk_src = LCD_CLK_SRC_DEFAULT;
  for (size_t i = 0; i < 8; i++) bus_config.data_gpio_nums[i] = this->data_pins_[i]->get_pin();
  bus_config.bus_width = 8;
  bus_config.max_transfer_bytes = static_cast<size_t>(this->width_) * this->height_ * BYTES_PER_PIXEL;
  bus_config.dma_burst_size = 64;

  esp_err_t err = esp_lcd_new_i80_bus(&bus_config, &this->bus_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_lcd_new_i80_bus failed: %d", (int) err);
    this->mark_failed();
    return;
  }

  esp_lcd_panel_io_i80_config_t io_config{};
  io_config.cs_gpio_num = this->cs_pin_ != nullptr ? this->cs_pin_->get_pin() : -1;
  io_config.pclk_hz = this->pclk_hz_;
  // Every transfer is waited on before its source buffer is handed back (see
  // draw_pixels_at), so nothing is ever queued behind anything else - depth 2
  // is plenty and keeps the DMA descriptor allocation small.
  io_config.trans_queue_depth = 2;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.dc_levels.dc_idle_level = 0;
  io_config.dc_levels.dc_cmd_level = 0;
  io_config.dc_levels.dc_dummy_level = 0;
  io_config.dc_levels.dc_data_level = 1;

  err = esp_lcd_new_panel_io_i80(this->bus_, &io_config, &this->io_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_lcd_new_panel_io_i80 failed: %d", (int) err);
    this->mark_failed();
    return;
  }

  // esp_lcd_panel_io_tx_color() is asynchronous: it hands the pointer to the
  // GDMA engine and returns immediately. Without this callback there is no way
  // to know when the DMA has finished reading the source buffer, and the
  // caller (LVGL) will start overwriting it for the next frame while it is
  // still being shifted out - which is exactly what produced tearing and
  // flickering artefacts on this panel.
  esp_lcd_panel_io_callbacks_t cbs{};
  cbs.on_color_trans_done = &ILI9341I80Display::trans_done_cb_;
  esp_lcd_panel_io_register_event_callbacks(this->io_handle_, &cbs, this);

  // No hardware reset line on the Rev6 (PIN_LCD_RST = -1), so a software
  // reset is the only way to get the panel into a known state. LovyanGFX's
  // Panel_ILI9341 does the same when pin_rst is -1. Must precede the init
  // table; SWRESET needs ~5ms but 150 is safe and only runs once at boot.
  if (this->reset_pin_ == nullptr) {
    esp_lcd_panel_io_tx_param(this->io_handle_, ILI9XXX_SWRESET, nullptr, 0);
    delay(150);
  }

  this->write_init_sequence_();

  if (this->invert_colors_) {
    uint8_t inv_cmd = ILI9XXX_INVON;
    esp_lcd_panel_io_tx_param(this->io_handle_, inv_cmd, nullptr, 0);
  }

  this->init_internal_(static_cast<uint32_t>(this->width_) * this->height_ * BYTES_PER_PIXEL);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate %d x %d display buffer", this->width_, this->height_);
    this->mark_failed();
  }
}

void ILI9341I80Display::write_init_sequence_() {
  // Same {cmd, len|delay-flag, args...} encoding ili9xxx uses: bit 0x80 of
  // the length byte means "no data, then wait ~120ms" for SLPOUT/DISPON.
  const uint8_t *addr = ili9xxx::INITCMD_ILI9341;
  while (true) {
    uint8_t cmd = *addr++;
    if (cmd == 0x00) break;
    uint8_t len_and_flag = *addr++;
    bool has_delay = (len_and_flag & 0x80) != 0;
    uint8_t len = len_and_flag & 0x7F;
    esp_lcd_panel_io_tx_param(this->io_handle_, cmd, len ? addr : nullptr, len);
    addr += len;
    if (has_delay) delay(150);
  }
}

void ILI9341I80Display::set_addr_window_(int x1, int y1, int x2, int y2) {
  uint8_t caset[4] = {static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1 & 0xFF),
                      static_cast<uint8_t>(x2 >> 8), static_cast<uint8_t>(x2 & 0xFF)};
  uint8_t paset[4] = {static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1 & 0xFF),
                      static_cast<uint8_t>(y2 >> 8), static_cast<uint8_t>(y2 & 0xFF)};
  esp_lcd_panel_io_tx_param(this->io_handle_, ILI9XXX_CASET, caset, sizeof(caset));
  esp_lcd_panel_io_tx_param(this->io_handle_, ILI9XXX_PASET, paset, sizeof(paset));
}

void ILI9341I80Display::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= this->width_ || y < 0 || y >= this->height_ || this->buffer_ == nullptr) return;
  int ex = this->mirror_x_ ? (this->width_ - 1 - x) : x;
  int ey = this->mirror_y_ ? (this->height_ - 1 - y) : y;
  uint16_t rgb565 = display::ColorUtil::color_to_565(color);
  size_t offset = (static_cast<size_t>(ey) * this->width_ + ex) * BYTES_PER_PIXEL;
  // Panel expects big-endian RGB565 over the data bus.
  this->buffer_[offset] = static_cast<uint8_t>(rgb565 >> 8);
  this->buffer_[offset + 1] = static_cast<uint8_t>(rgb565 & 0xFF);
}

bool ILI9341I80Display::trans_done_cb_(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event,
                                       void *user_ctx) {
  static_cast<ILI9341I80Display *>(user_ctx)->trans_done_ = true;
  return false;  // no higher-priority task woken
}

void ILI9341I80Display::wait_trans_() {
  if (this->trans_done_) return;
  uint32_t start = millis();
  while (!this->trans_done_) {
    // A full 240x320 frame at 20 MHz is ~7.7 ms, a typical dirty rectangle far
    // less, so this spin is short. The generous timeout only exists so a lost
    // interrupt cannot wedge the display loop forever.
    if ((millis() - start) > 500) {
      ESP_LOGW(TAG, "LCD transfer did not complete within 500ms");
      this->trans_done_ = true;
      break;
    }
    delayMicroseconds(20);
  }
}

void ILI9341I80Display::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                       display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                                       int x_offset, int y_offset, int x_pad) {
  if (this->io_handle_ == nullptr || w <= 0 || h <= 0) return;
  // Clip to the panel; esp_lcd would happily program an out-of-range window.
  if (x_start >= this->width_ || y_start >= this->height_ || x_start + w <= 0 || y_start + h <= 0) return;

  const size_t px = static_cast<size_t>(w) * h;
  const uint8_t *src = ptr;

  // Fast path: this is what LVGL hands us - packed RGB565, big-endian (the
  // byte order this panel wants on the 8-bit bus), no offsets or padding. The
  // source buffer can go straight to the DMA with no copy and no conversion.
  bool direct = bitness == display::COLOR_BITNESS_565 && big_endian && !this->mirror_x_ && !this->mirror_y_ &&
                x_offset == 0 && y_offset == 0 && x_pad == 0;

  if (!direct) {
    // Slow path (mirroring, padded strides, or a non-565 source). Convert once
    // into a scratch buffer instead of falling back to the base class, which
    // would go through draw_absolute_pixel_internal() one pixel at a time.
    this->scratch_.resize(px * BYTES_PER_PIXEL);
    const size_t line_stride = static_cast<size_t>(x_offset) + w + x_pad;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t si = (static_cast<size_t>(y_offset) + y) * line_stride + x_offset + x;
        uint16_t c;
        switch (bitness) {
          case display::COLOR_BITNESS_565:
            c = big_endian ? (ptr[si * 2] << 8) | ptr[si * 2 + 1] : ptr[si * 2] | (ptr[si * 2 + 1] << 8);
            break;
          case display::COLOR_BITNESS_888:
            c = display::ColorUtil::color_to_565(Color(ptr[si * 3], ptr[si * 3 + 1], ptr[si * 3 + 2]));
            break;
          default:
            c = ptr[si] ? 0xFFFF : 0x0000;
            break;
        }
        int dx = this->mirror_x_ ? (w - 1 - x) : x;
        int dy = this->mirror_y_ ? (h - 1 - y) : y;
        size_t di = (static_cast<size_t>(dy) * w + dx) * BYTES_PER_PIXEL;
        this->scratch_[di] = static_cast<uint8_t>(c >> 8);
        this->scratch_[di + 1] = static_cast<uint8_t>(c & 0xFF);
      }
    }
    src = this->scratch_.data();
  }

  // Previous transfer must be off the bus before the window is reprogrammed.
  this->wait_trans_();
  this->set_addr_window_(x_start, y_start, x_start + w - 1, y_start + h - 1);
  this->trans_done_ = false;
  esp_err_t err = esp_lcd_panel_io_tx_color(this->io_handle_, ILI9XXX_RAMWR, src, px * BYTES_PER_PIXEL);
  if (err != ESP_OK) {
    this->trans_done_ = true;
    ESP_LOGW(TAG, "esp_lcd_panel_io_tx_color failed: %d", (int) err);
    return;
  }
  // Must block here: as soon as this returns, LVGL is free to render the next
  // frame into the very buffer the DMA is still reading. This is the same
  // synchronous behaviour as the reference firmware's Arduino_GFX path, which
  // is the driver it ships as its default.
  this->wait_trans_();
}

void ILI9341I80Display::update() {
  // Nothing to push here. Pixels reach the panel through draw_pixels_at() as
  // LVGL invalidates regions; blasting the whole framebuffer on a timer (what
  // this used to do, every 16 ms, whether anything had changed or not) is what
  // saturated the parallel bus and tore frames.
  this->do_update_();
}

void ILI9341I80Display::enter_sleep() {
  if (this->io_handle_ == nullptr) return;
  this->wait_trans_();
  esp_lcd_panel_io_tx_param(this->io_handle_, ILI9XXX_DISPOFF, nullptr, 0);
  esp_lcd_panel_io_tx_param(this->io_handle_, ILI9XXX_SLPIN, nullptr, 0);
  ESP_LOGI(TAG, "Panel: DISPOFF + SLPIN");
}

void ILI9341I80Display::exit_sleep() {
  if (this->io_handle_ == nullptr) return;
  esp_lcd_panel_io_tx_param(this->io_handle_, ILI9XXX_SLPOUT, nullptr, 0);
  delay(130);  // ILI9341: >=120 ms nach SLPOUT bevor der naechste Befehl kommt
  esp_lcd_panel_io_tx_param(this->io_handle_, ILI9XXX_DISPON, nullptr, 0);
  ESP_LOGI(TAG, "Panel: SLPOUT + DISPON");
}

void ILI9341I80Display::flush_buffer_() {
  if (this->buffer_ == nullptr) return;
  this->wait_trans_();
  this->set_addr_window_(0, 0, this->width_ - 1, this->height_ - 1);
  size_t total = static_cast<size_t>(this->width_) * this->height_ * BYTES_PER_PIXEL;
  this->trans_done_ = false;
  if (esp_lcd_panel_io_tx_color(this->io_handle_, ILI9XXX_RAMWR, this->buffer_, total) != ESP_OK)
    this->trans_done_ = true;
  this->wait_trans_();
}

void ILI9341I80Display::dump_config() {
  ESP_LOGCONFIG(TAG, "ILI9341 (8-bit parallel/i80):");
  ESP_LOGCONFIG(TAG, "  Dimensions: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Bus clock: %" PRIu32 " Hz", this->pclk_hz_);
  ESP_LOGCONFIG(TAG, "  Invert colors: %s", YESNO(this->invert_colors_));
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup failed!");
  }
}

}  // namespace ili9341_i80
}  // namespace esphome

#endif
