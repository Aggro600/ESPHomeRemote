#include "sd_card.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

extern "C" {
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
}

namespace esphome {
namespace sd_card {

static const char *const TAG = "sd_card";

// eigener SPI-Host, damit wir nicht mit ESPHomes spi-Component kollidieren
static constexpr spi_host_device_t SD_SPI_HOST = SPI3_HOST;

void SdCard::setup() {
  if (this->power_pin_ != nullptr) {
    this->power_pin_->setup();
    this->power_pin_->digital_write(true);  // active-low -> HIGH = Karte AUS
  }
  ESP_LOGCONFIG(TAG, "microSD (SPI) vorbereitet - noch nicht gemountet");
  if (this->mount_on_boot_) {
    this->set_timeout(1500, [this]() { this->mount(); });
  }
}

void SdCard::dump_config() {
  ESP_LOGCONFIG(TAG, "microSD-Karte (SPI):");
  ESP_LOGCONFIG(TAG, "  CLK=%u MOSI=%u MISO=%u CS=%u", this->clk_pin_, this->mosi_pin_, this->miso_pin_,
                this->cs_pin_);
  if (this->power_pin_ != nullptr)
    LOG_PIN("  Power-Pin (active-low): ", this->power_pin_);
  ESP_LOGCONFIG(TAG, "  Mount-Punkt: %s   Takt: %u kHz", this->mount_point_.c_str(), this->freq_khz_);
  ESP_LOGCONFIG(TAG, "  Zustand: %s", this->mounted_ ? "gemountet" : "nicht gemountet");
  if (this->mounted_) {
    ESP_LOGCONFIG(TAG, "  Karte: %s (%s), %.2f GB", this->card_name_.c_str(), this->card_type_.c_str(),
                  this->card_size_ / (1024.0 * 1024.0 * 1024.0));
  }
}

bool SdCard::mount() {
  if (this->mounted_) {
    ESP_LOGD(TAG, "schon gemountet");
    return true;
  }

  // 1. Strom an (active-low)
  if (this->power_pin_ != nullptr) {
    this->power_pin_->digital_write(false);
    delay(20);  // Karten-Regler einschwingen
  }

  // 2. SPI-Bus
  if (!this->bus_inited_) {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = this->mosi_pin_;
    buscfg.miso_io_num = this->miso_pin_;
    buscfg.sclk_io_num = this->clk_pin_;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 4000;
    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
      if (this->power_pin_ != nullptr)
        this->power_pin_->digital_write(true);
      return false;
    }
    this->bus_inited_ = true;
  }

  // 3. FATFS mounten
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_SPI_HOST;
  host.max_freq_khz = (int) this->freq_khz_;

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = (gpio_num_t) this->cs_pin_;
  slot.host_id = SD_SPI_HOST;

  esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
  mcfg.format_if_mount_failed = false;
  mcfg.max_files = 4;
  mcfg.allocation_unit_size = 16 * 1024;

  esp_err_t err =
      esp_vfs_fat_sdspi_mount(this->mount_point_.c_str(), &host, &slot, &mcfg, &this->card_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Mount fehlgeschlagen (%s) - Karte drin? FAT32?", esp_err_to_name(err));
    spi_bus_free(SD_SPI_HOST);
    this->bus_inited_ = false;
    if (this->power_pin_ != nullptr)
      this->power_pin_->digital_write(true);
    return false;
  }

  this->mounted_ = true;
  this->card_size_ = (uint64_t) this->card_->csd.capacity * this->card_->csd.sector_size;
  this->card_name_ = std::string(this->card_->cid.name);
  this->card_type_ = (this->card_->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC";
  ESP_LOGI(TAG, "Karte gemountet: %s (%s), %.2f GB", this->card_name_.c_str(), this->card_type_.c_str(),
           this->card_size_ / (1024.0 * 1024.0 * 1024.0));
  return true;
}

void SdCard::unmount() {
  this->write_close();
  if (!this->mounted_)
    return;
  esp_vfs_fat_sdcard_unmount(this->mount_point_.c_str(), this->card_);
  this->card_ = nullptr;
  this->mounted_ = false;
  if (this->bus_inited_) {
    spi_bus_free(SD_SPI_HOST);
    this->bus_inited_ = false;
  }
  if (this->power_pin_ != nullptr)
    this->power_pin_->digital_write(true);  // Strom aus
  ESP_LOGI(TAG, "Karte ausgehaengt, stromlos");
}

uint64_t SdCard::free_bytes() {
  if (!this->mounted_)
    return 0;
  FATFS *fs;
  DWORD free_clusters;
  std::string drv = this->mount_point_ + "/";
  if (f_getfree(drv.c_str(), &free_clusters, &fs) != FR_OK)
    return 0;
  uint64_t sect = (uint64_t) free_clusters * fs->csize;
  return sect * 512ULL;
}

std::string SdCard::full_(const std::string &path) const {
  if (!path.empty() && path[0] == '/')
    return this->mount_point_ + path;
  return this->mount_point_ + "/" + path;
}

std::vector<SdEntry> SdCard::list_dir(const std::string &path) {
  std::vector<SdEntry> out;
  if (!this->mounted_)
    return out;
  std::string p = this->full_(path);
  DIR *dir = opendir(p.c_str());
  if (dir == nullptr) {
    ESP_LOGW(TAG, "opendir(%s): %s", p.c_str(), strerror(errno));
    return out;
  }
  struct dirent *de;
  while ((de = readdir(dir)) != nullptr) {
    SdEntry e;
    e.name = de->d_name;
    e.is_dir = (de->d_type == DT_DIR);
    e.size = 0;
    if (!e.is_dir) {
      struct stat st;
      std::string fp = p + "/" + e.name;
      if (stat(fp.c_str(), &st) == 0)
        e.size = st.st_size;
    }
    out.push_back(e);
  }
  closedir(dir);
  return out;
}

bool SdCard::file_exists(const std::string &path) {
  if (!this->mounted_)
    return false;
  struct stat st;
  return stat(this->full_(path).c_str(), &st) == 0;
}

bool SdCard::read_text(const std::string &path, std::string &out, size_t max_bytes) {
  out.clear();
  if (!this->mounted_)
    return false;
  FILE *f = fopen(this->full_(path).c_str(), "r");
  if (f == nullptr) {
    ESP_LOGW(TAG, "fopen(%s) r: %s", path.c_str(), strerror(errno));
    return false;
  }
  std::vector<char> buf(max_bytes + 1);
  size_t n = fread(buf.data(), 1, max_bytes, f);
  fclose(f);
  buf[n] = '\0';
  out.assign(buf.data(), n);
  return true;
}

bool SdCard::read_binary(const std::string &path, std::vector<uint8_t> &out, size_t max_bytes) {
  out.clear();
  if (!this->mounted_)
    return false;
  FILE *f = fopen(this->full_(path).c_str(), "rb");
  if (f == nullptr) {
    ESP_LOGW(TAG, "fopen(%s) rb: %s", path.c_str(), strerror(errno));
    return false;
  }
  out.resize(max_bytes);
  size_t n = fread(out.data(), 1, max_bytes, f);
  fclose(f);
  out.resize(n);
  return n > 0;
}

bool SdCard::write_text(const std::string &path, const std::string &data, bool append) {
  if (!this->mounted_)
    return false;
  if (!append) {
    // Fehlende Ordner anlegen (wie mkdir -p), z. B. beim Hochladen nach /covers/... (Stand 2026-09-21).
    const std::string voll = this->full_(path);
    for (size_t pos = 1; (pos = voll.find('/', pos)) != std::string::npos; pos++)
      ::mkdir(voll.substr(0, pos).c_str(), 0777);   // Fehler (existiert schon) ignorieren
  }
  FILE *f = fopen(this->full_(path).c_str(), append ? "a" : "w");
  if (f == nullptr) {
    ESP_LOGW(TAG, "fopen(%s) w: %s", path.c_str(), strerror(errno));
    return false;
  }
  size_t n = fwrite(data.data(), 1, data.size(), f);
  fclose(f);
  return n == data.size();
}

bool SdCard::remove_file(const std::string &path) {
  if (!this->mounted_)
    return false;
  return ::remove(this->full_(path).c_str()) == 0;
}

bool SdCard::write_open(const std::string &path) {
  this->write_close();
  if (!this->mounted_)
    return false;
  const std::string voll = this->full_(path);
  for (size_t pos = 1; (pos = voll.find('/', pos)) != std::string::npos; pos++)
    ::mkdir(voll.substr(0, pos).c_str(), 0777);   // fehlende Ordner anlegen, Fehler ignorieren
  this->wfile_ = fopen(voll.c_str(), "wb");
  if (this->wfile_ == nullptr) {
    ESP_LOGW(TAG, "fopen(%s) wb: %s", path.c_str(), strerror(errno));
    return false;
  }
  return true;
}

bool SdCard::write_chunk(const uint8_t *data, size_t len) {
  if (this->wfile_ == nullptr)
    return false;
  return fwrite(data, 1, len, this->wfile_) == len;
}

void SdCard::write_close() {
  if (this->wfile_ != nullptr) {
    fclose(this->wfile_);
    this->wfile_ = nullptr;
  }
}

bool SdCard::make_dir(const std::string &path) {
  if (!this->mounted_)
    return false;
  return ::mkdir(this->full_(path).c_str(), 0777) == 0;
}

}  // namespace sd_card
}  // namespace esphome

#endif  // USE_ESP32
