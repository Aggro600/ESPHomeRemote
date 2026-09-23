#pragma once
#include <cstdio>

// microSD-Karte fuer die Open Remote (Rev6), SPI-Modus.
//
// Rev6-Besonderheit: die drei Datenleitungen der SD-Karte teilen sich die Pins
// mit dem I2S-Sprachmikrofon (GPIO15=SCK/BCLK, GPIO17=MOSI/WS, GPIO7=MISO/DIN),
// getrennt nur durch 4k7-Serienwiderstaende (R45-47). Beides gleichzeitig geht
// nicht. Diese Testversion (open-remote-sd-test.yaml) hat das Mikrofon deshalb
// deaktiviert; die Pins gehoeren hier exklusiv der SD-Karte.
//
// Stromversorgung: SD_3V3 haengt an Q7 (P-MOSFET), Gate an SD_EN (GPIO16):
//   GPIO16 LOW  -> Q7 EIN  -> Karte hat Strom
//   GPIO16 HIGH -> Q7 AUS  -> Karte stromlos
//
// Kartenerkennung (SD_DET) laeuft ueber den TCA8418 (ROW6) - nicht an einem
// ESP-GPIO, daher hier nicht direkt auswertbar.

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/gpio.h"
#include <string>
#include <vector>

extern "C" {
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
}

namespace esphome {
namespace sd_card {

class SdHttp;

struct SdEntry {
  std::string name;
  bool is_dir;
  size_t size;
};

class SdCard : public Component {
 public:
  void set_clk_pin(uint8_t p) { clk_pin_ = p; }
  void set_mosi_pin(uint8_t p) { mosi_pin_ = p; }
  void set_miso_pin(uint8_t p) { miso_pin_ = p; }
  void set_cs_pin(uint8_t p) { cs_pin_ = p; }
  void set_power_pin(InternalGPIOPin *p) { power_pin_ = p; }  // active-low (LOW = Karte an)
  void set_mount_point(const std::string &mp) { mount_point_ = mp; }
  void set_frequency_khz(uint32_t khz) { freq_khz_ = khz; }
  void set_mount_on_boot(bool b) { mount_on_boot_ = b; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // --- API fuer Lambdas / Automations ---
  bool mount();
  void unmount();
  bool is_mounted() const { return mounted_; }
  sdmmc_card_t *card() const { return card_; }   // fuer Messungen (sdmmc_read_sectors)
  uint32_t frequency_khz() const { return freq_khz_; }

  // Karteninfo (nur gueltig wenn gemountet)
  uint64_t card_size_bytes() const { return card_size_; }
  uint64_t free_bytes();
  std::string card_name() const { return card_name_; }
  std::string card_type() const { return card_type_; }

  std::vector<SdEntry> list_dir(const std::string &path);
  bool file_exists(const std::string &path);
  bool read_text(const std::string &path, std::string &out, size_t max_bytes = 4096);
  // Rohe Bytes (z. B. .rgb565-Wallpaper). Liest bis zu max_bytes; out wird auf
  // die tatsaechliche Laenge gesetzt.
  bool read_binary(const std::string &path, std::vector<uint8_t> &out, size_t max_bytes);
  bool write_text(const std::string &path, const std::string &data, bool append = false);
  // Schreiben in Stuecken bei OFFENER Datei (Upload): open/close pro Stueck kostete ~1 s je 1,4 KB.
  bool write_open(const std::string &path);
  bool write_chunk(const uint8_t *data, size_t len);
  void write_close();
  bool remove_file(const std::string &path);
  bool make_dir(const std::string &path);

#if defined(USE_NETWORK) && !defined(USE_ZEPHYR)
  // Zeitlich begrenzter Konfig-HTTP-Endpunkt (siehe sd_http.h). timeout_s = 0
  // heisst "kein Auto-Aus".
  void http_enable(uint32_t timeout_s);
  void http_disable();
  bool http_active() const { return http_active_; }
#endif

 protected:
  std::string full_(const std::string &path) const;

  uint8_t clk_pin_{15}, mosi_pin_{17}, miso_pin_{7}, cs_pin_{18};
  InternalGPIOPin *power_pin_{nullptr};
  std::string mount_point_{"/sd"};
  uint32_t freq_khz_{10000};
  bool mount_on_boot_{false};

  bool mounted_{false};
  FILE *wfile_{nullptr};
  bool bus_inited_{false};
  sdmmc_card_t *card_{nullptr};
  uint64_t card_size_{0};
  std::string card_name_, card_type_;

#if defined(USE_NETWORK) && !defined(USE_ZEPHYR)
  SdHttp *http_handler_{nullptr};
  bool http_registered_{false};
  bool http_active_{false};
#endif
};

}  // namespace sd_card
}  // namespace esphome

#endif  // USE_ESP32
