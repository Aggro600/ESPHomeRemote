#pragma once
// Logo-/Cover-Bilder von der SD-Karte (nur Sabrinas Remote, Stand 2026-09-21).
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
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "driver/gpio.h"
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

}  // namespace sdbilder
