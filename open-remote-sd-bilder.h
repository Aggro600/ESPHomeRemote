#pragma once
// Logo-/Cover-Bilder von der SD-Karte (nur mit bestueckter microSD-Karte).
//
// Dateien (Ordner /covers auf der Karte, erzeugt von tools/make_sd_assets.py):
//   /covers/<app>_player.rle      240 x 320, RGB565 little-endian, lauflaengenkodiert "RL16" (Cover der Medienseite)
//   /covers/<app>_home.rle        216 x 172, gleiches Format (Cover-Banner der Startseite)
// (Ersatzweise werden gleichnamige rohe .rgb565-Dateien gelesen - die Karte liest aber nur ~5 KB/s.)
// <app> = kleingeschriebener Name ohne Sonderzeichen, "+" wird zu "plus" (Netflix -> netflix,
// Disney+ -> disneyplus, Prime Video -> primevideo).
//
// Die Bilder liegen nach dem Laden im PSRAM (Cache); die Karte wird nur kurz eingebunden und sofort
// wieder ausgeworfen, damit das Mikrofon (gleiche Anschluesse) wieder frei ist.
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "esphome/components/sd_card/sd_card.h"
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

namespace sdbilder {

struct Bild {
  std::vector<uint8_t> daten;
  lv_image_dsc_t dsc{};
  bool ok{false};
};

struct App {
  Bild player;
  Bild home;
  uint32_t fehlt_bis{0};   // millis(): bis dahin nicht erneut versuchen (Karte/Datei fehlte)
};

inline std::map<std::string, App> &cache() {
  static std::map<std::string, App> c;
  return c;
}

// "Disney+" -> "disneyplus", "Prime Video" -> "primevideo"
inline std::string slug(const std::string &app) {
  std::string s;
  for (char ch : app) {
    if (std::isalnum((unsigned char) ch)) s += (char) std::tolower((unsigned char) ch);
    else if (ch == '+') s += "plus";
  }
  return s;
}

// Deskriptor fuer ein fertig im Speicher liegendes RGB565-Bild fuellen.
inline void fuellen(Bild &b, int w, int h) {
  b.dsc = {};
  b.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  b.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  b.dsc.header.w = w;
  b.dsc.header.h = h;
  b.dsc.header.stride = w * 2;
  b.dsc.data_size = (size_t) w * h * 2;
  b.dsc.data = b.daten.data();
  b.ok = true;
}

// Lauflaengenkodiertes Bild ("RL16", tools/make_sd_assets.py): winzige Dateien, weil die Karte nur ~5 KB/s liest.
inline bool laden_rle(esphome::sd_card::SdCard *sd, const std::string &pfad, int w, int h, Bild &b) {
  b.ok = false;
  std::vector<uint8_t> roh;
  if (!sd->read_binary(pfad, roh, 65536)) return false;
  if (roh.size() < 8 || roh[0] != 'R' || roh[1] != 'L' || roh[2] != '1' || roh[3] != '6') return false;
  const int fw = roh[4] | (roh[5] << 8), fh = roh[6] | (roh[7] << 8);
  if (fw != w || fh != h) return false;
  const size_t gesamt = (size_t) w * h;
  b.daten.assign(gesamt * 2, 0);
  uint16_t *aus = reinterpret_cast<uint16_t *>(b.daten.data());
  size_t pos = 8, px = 0;
  while (pos + 4 <= roh.size() && px < gesamt) {
    size_t n = roh[pos] | (roh[pos + 1] << 8);
    const uint16_t v = roh[pos + 2] | (roh[pos + 3] << 8);
    pos += 4;
    if (px + n > gesamt) n = gesamt - px;
    for (size_t i = 0; i < n; i++) aus[px++] = v;
  }
  if (px != gesamt) {
    b.daten.clear();
    b.daten.shrink_to_fit();
    return false;
  }
  fuellen(b, w, h);
  return true;
}

// Bild "<basis>.rle" (bevorzugt) oder "<basis>.rgb565" (roh, langsam) laden.
inline bool laden_bild(esphome::sd_card::SdCard *sd, const std::string &basis, int w, int h, Bild &b);

// Rohes RGB565-Bild von der (eingebundenen) Karte in den Cache lesen und den LVGL-Deskriptor fuellen.
inline bool laden(esphome::sd_card::SdCard *sd, const std::string &pfad, int w, int h, Bild &b) {
  const size_t soll = (size_t) w * (size_t) h * 2;
  b.ok = false;
  if (!sd->read_binary(pfad, b.daten, soll + 64)) return false;
  if (b.daten.size() != soll) {   // falsche Groesse: lieber nichts anzeigen als Muell
    b.daten.clear();
    b.daten.shrink_to_fit();
    return false;
  }
  fuellen(b, w, h);
  return true;
}

inline bool laden_bild(esphome::sd_card::SdCard *sd, const std::string &basis, int w, int h, Bild &b) {
  if (laden_rle(sd, basis + ".rle", w, h, b)) return true;
  return laden(sd, basis + ".rgb565", w, h, b);
}

// ---- Laden in einem eigenen Task (Stand 2026-09-21) ---------------------------------------------
// Die Karte liest mit den 4k7-Serienwiderstaenden an SCK/MOSI/MISO nur langsam (gemessen ~5 KB/s bei
// 8 MHz). Ein Lesen im Hauptprogramm blockierte laenger als der Task-Watchdog erlaubt -> Absturz und
// Neustart-Schleife, solange Netflix lief. Deshalb: mount, lesen und unmount im Hintergrund-Task; das
// Hauptprogramm wartet nur auf das Ende (busy() = false) und uebernimmt dann das Ergebnis.
inline volatile bool &busy() {
  static volatile bool b = false;
  return b;
}
inline App &ergebnis() {   // wird vom Task gefuellt, erst nach busy() == false lesen
  static App a;
  return a;
}
struct Auftrag {
  esphome::sd_card::SdCard *sd;
  std::string slug;
};
inline void task_fn(void *arg) {
  Auftrag *a = static_cast<Auftrag *>(arg);
  App &e = ergebnis();
  e = App();
  bool ok = false;
  if (a->sd->mount()) {
    const bool p = laden_bild(a->sd, "/covers/" + a->slug + "_player", 240, 320, e.player);
    const bool h = laden_bild(a->sd, "/covers/" + a->slug + "_home", 216, 172, e.home);
    ok = p || h;
    a->sd->unmount();
  }
  e.fehlt_bis = ok ? 0 : 1;   // 1 = "fehlgeschlagen" (die Zeit setzt das Hauptprogramm)
  delete a;
  busy() = false;
  vTaskDelete(nullptr);
}
inline bool starte(esphome::sd_card::SdCard *sd, const std::string &slug) {
  if (busy()) return false;
  busy() = true;
  if (xTaskCreate(task_fn, "sdlogo", 8192, new Auftrag{sd, slug}, 1, nullptr) != pdPASS) {
    busy() = false;
    return false;
  }
  return true;
}

// ---- Geschwindigkeitstest der Karte (Stand 2026-09-21) --------------------------------------------
// Misst bei mehreren SPI-Takten (a) fread in 4-KB-Stuecken in einen DMA-faehigen Puffer und (b) rohe
// Sektorlesezugriffe, um zu sehen, ob die Karte oder die Anbindung bremst. Ergebnis im Log (Tag "sdtest").
inline void bench_task(void *arg) {
  // Geschwindigkeitstest (HA-Knopf "SD: Geschwindigkeitstest", Log-Tag "sdtest"). Ursache der frueheren 83 ms je
  // Lesebefehl: Bei abgeschaltetem Mikrofon (MIC_VDD = 0 V) klemmt dessen ESD-Diode die gemeinsame Leitung MISO
  // (GPIO7) nach Low, und ESP-IDF wartet vor jedem Befehl bis zu 40 ms, dass MISO hoch geht (poll_busy). Mit
  // eingeschaltetem Mikrofon: 0,2-3 ms je Befehl. Der Test laeuft darum bei eingeschalteter Versorgung; pruefen
  // laesst sich das hier an den Werten "Befehl" (soll < 5 ms sein) und "MISO-Pegel" (soll 1 sein).
  auto *sd = static_cast<esphome::sd_card::SdCard *>(arg);
  static const uint32_t takte[] = {1000, 8000, 20000};
  const uint32_t alt = sd->frequency_khz();
  uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (buf == nullptr) {
    ESP_LOGW("sdtest", "kein DMA-Puffer");
  } else {
    sd->unmount();
    for (uint32_t f : takte) {
      sd->set_frequency_khz(f);
      const int64_t t0 = esp_timer_get_time();
      if (!sd->mount()) {
        ESP_LOGW("sdtest", "%u kHz: Mount fehlgeschlagen", (unsigned) f);
        continue;
      }
      const int t_mount = (int) ((esp_timer_get_time() - t0) / 1000);
      sdmmc_card_t *c = sd->card();
      const int pegel = gpio_get_level(GPIO_NUM_7);
      // Zeit je Einzelbefehl (1 Sektor)
      int64_t t1 = esp_timer_get_time();
      for (int i = 0; i < 16; i++) sdmmc_read_sectors(c, buf, 40000 + i, 1);
      const int t_befehl = (int) ((esp_timer_get_time() - t1) / 16);
      // rohe Sektoren: 8 x 4 KB
      t1 = esp_timer_get_time();
      int fehler = 0;
      for (int i = 0; i < 8; i++)
        if (sdmmc_read_sectors(c, buf, 4096 + i * 8, 8) != ESP_OK) fehler++;
      const int t_sekt = (int) ((esp_timer_get_time() - t1) / 1000);
      // Datei ueber fread (FatFS), bis 32 KB
      size_t gelesen = 0;
      t1 = esp_timer_get_time();
      FILE *fp = fopen("/sd/covers/netflix_player.rgb565", "rb");
      if (fp != nullptr) {
        size_t n;
        while (gelesen < 32768 && (n = fread(buf, 1, 4096, fp)) > 0) gelesen += n;
        fclose(fp);
      }
      const int t_datei = (int) ((esp_timer_get_time() - t1) / 1000);
      ESP_LOGI("sdtest",
               "%5u kHz: Mount %d ms | MISO-Pegel %d | Befehl %d us | Sektoren 32 KB in %d ms = %.0f KB/s (Fehler %d) | "
               "fread %u B in %d ms = %.0f KB/s",
               (unsigned) f, t_mount, pegel, t_befehl, t_sekt, t_sekt ? 32768 / 1.024 / t_sekt : 0.0, fehler,
               (unsigned) gelesen, t_datei, t_datei ? gelesen / 1.024 / t_datei : 0.0);
      sd->unmount();
    }
    heap_caps_free(buf);
  }
  sd->set_frequency_khz(alt);
  busy() = false;
  vTaskDelete(nullptr);
}
inline bool bench_starten(esphome::sd_card::SdCard *sd) {
  if (busy()) return false;
  busy() = true;
  if (xTaskCreate(bench_task, "sdtest", 6144, sd, 1, nullptr) != pdPASS) {
    busy() = false;
    return false;
  }
  return true;
}

}  // namespace sdbilder
