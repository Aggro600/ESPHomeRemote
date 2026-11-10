#pragma once
// =====================================================================================================================
// Schnellweg (2026-11-05): die Weck-Taste geht ~0,2 s nach dem Aufwachen ans Dock statt nach ~1,7 s.
//
// Ein C++-Konstruktor startet ganz am Anfang der Firmware - bevor ESPHome seine Objekte und die ganze Oberflaeche
// (LVGL, ~0,8 s) anlegt - einen eigenen Task. Der liest beim Aufwachen per Taste oder Bewegung den Tasten-Chip (TCA8418,
// I2C), sucht die Taste in der Weck-Tabelle und funkt deren fertige Pakete per ESP-NOW ans Dock.
//   - Weck-Tabelle: key_config::weck_tabelle_speichern legt vor dem Tiefschlaf (on_shutdown) fuer die aktuelle Aktivitaet
//     die Pakete jeder Taste ab, die weck_frueh senden wuerde (NVS "tasten"/"wtab"). Nur Tasten ueber das Dock
//     (Bluetooth ueber das Dock, IR, 433 MHz) - alles andere geht wie bisher nach dem Start.
//   - Kanal, Schluessel und freie Nummer: dock_direct::g_schnell (RTC-Speicher). Die Nummern sind der Anfang des
//     naechsten, im NVS schon reservierten Blocks; dock_direct setzt danach dahinter fort - nie doppelt.
//   - Die gelesenen Tastenereignisse uebernimmt der Tasten-Treiber (tca8418::g_frueh_tasten), gesendete Weck-Taste =
//     weck_erledigt. Bestaetigt das Dock nicht, sendet der normale Weg die Taste wie bisher.
//   - Aufwachen durch Bewegung (Hochnehmen): Funk gleich vorbereiten und den Tasten-Chip alle 20 ms ansehen, bis
//     gedrueckt wird oder ESPHome startet.
//   - ESPHome wartet an seiner ersten Komponente (on_boot 9000: schnellweg::abwarten), bis Funk und I2C frei sind.
// Hardware: OMOTE/Open Remote Rev6 - I2C SDA 20 / SCL 19, TCA8418 0x34, Display-Rail GPIO38 (LOW = an, traegt die
// I2C-Pullups), Display-Beleuchtung GPIO9 (HIGH = aus), Weckpins ext1 GPIO8 (Taste) / GPIO2 (Bewegung).
// Ganz schnell (~40 ms statt ~580 ms bis hierher) nur mit dem Bootloader ohne Pruefung beim Aufwachen
// (CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP) - der kommt nur per USB-Flash auf die Remote.
// =====================================================================================================================
#include <esp_attr.h>
#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <mbedtls/md.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace schnellweg {

inline volatile bool laeuft = false;
inline uint32_t bootzeit_c = 0, bootzeit_e = 0;   // Start/Ende des Schnellwegs (ms, Fehlersuche Startzeit)
inline uint32_t rail_ms = 0;          // Display-Rail eingeschaltet (millis, on_boot 1100) - Rest der Einschwingzeit vor dem Display
inline volatile bool stopp = false;   // ESPHome startet seine Komponenten: Abfrage beenden
inline volatile bool cb_da = false;
inline volatile bool ack = false;

// Ergebnis fuer das Weckprotokoll (nach dem Start ausgegeben)
struct Bericht { uint32_t magic; uint8_t taste; uint8_t pakete; uint8_t ok; uint8_t bewegung; uint16_t ack_ms; };
static RTC_NOINIT_ATTR Bericht bericht;
static const uint32_t BERICHT_MAGIC = 0x53574245u;   // "SWBE"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void gesendet_cb(const esp_now_send_info_t *, esp_now_send_status_t st) {
#else
static void gesendet_cb(const uint8_t *, esp_now_send_status_t st) {
#endif
  ack = st == ESP_NOW_SEND_SUCCESS;
  cb_da = true;
}

static void funk_aus() {
  esp_now_deinit();
  esp_wifi_stop();
  esp_wifi_deinit();
}

static bool funk_an_(uint8_t kanal) {
  nvs_flash_init();   // PHY-Kalibrierdaten liegen im NVS (ESPHome ruft es spaeter nochmal - dann einfach ESP_OK)
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.nvs_enable = 0;
  if (esp_wifi_init(&cfg) != ESP_OK) return false;
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  if (esp_wifi_start() != ESP_OK) return false;
  esp_wifi_set_channel(kanal, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(gesendet_cb);
  return true;
}

// Funk an; klappt es nicht ganz, alles wieder abraeumen (ESPHome startet das WLAN spaeter selbst)
static bool funk_an(uint8_t kanal) {
  if (funk_an_(kanal)) return true;
  funk_aus();
  return false;
}

// Ein Paket wie dock_direct::einreihen_ bauen und senden; true = Dock hat bestaetigt
static bool senden(const uint8_t mac[6], uint8_t typ, uint32_t param, uint16_t anzahl, const uint8_t *inhalt,
                   uint16_t len, uint32_t nr) {
  auto &d = esphome::dock_direkt::g_schnell;
  std::vector<uint8_t> p;
  p.reserve(18 + len + 8);
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) p.push_back((uint8_t) (v >> (8 * i))); };
  p.push_back('O');
  p.push_back('R');
  p.push_back(1);
  p.push_back(typ);
  u32(esp_random());
  u32(nr);
  u32(param);
  p.push_back((uint8_t) (anzahl & 0xFF));
  p.push_back((uint8_t) (anzahl >> 8));
  p.insert(p.end(), inhalt, inhalt + len);
  uint8_t eigene[6], voll[32];
  esp_read_mac(eigene, ESP_MAC_WIFI_STA);
  mbedtls_md_context_t c;
  mbedtls_md_init(&c);
  mbedtls_md_setup(&c, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&c, d.schluessel, d.schluessel_len);
  mbedtls_md_hmac_update(&c, eigene, 6);
  mbedtls_md_hmac_update(&c, p.data(), p.size());
  mbedtls_md_hmac_finish(&c, voll);
  mbedtls_md_free(&c);
  p.insert(p.end(), voll, voll + 8);
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&peer);
  }
  for (int versuch = 0; versuch < 3; versuch++) {   // wie dock_direct: zweimal nachlegen
    cb_da = false;
    ack = false;
    if (esp_now_send(mac, p.data(), p.size()) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
    for (int i = 0; i < 60 && !cb_da; i++) vTaskDelay(pdMS_TO_TICKS(1));
    if (ack) return true;
  }
  return false;
}

// Tasten-Chip lesen. warten_ms > 0 (Bewegung): alle 20 ms nachsehen, bis gedrueckt wurde oder ESPHome startet.
static int tasten_lesen(uint8_t *ev, int max, uint32_t warten_ms) {
  // Display-Beleuchtung (GPIO9, invertiert) sicher aus, BEVOR ihre Haltung geloest wird (wie on_boot 1100)
  gpio_set_level(GPIO_NUM_9, 1);
  gpio_config_t bl = {};
  bl.pin_bit_mask = 1ULL << GPIO_NUM_9;
  bl.mode = GPIO_MODE_OUTPUT;
  bl.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&bl);
  gpio_set_level(GPIO_NUM_9, 1);
  gpio_hold_dis(GPIO_NUM_9);
  rtc_gpio_hold_dis(GPIO_NUM_9);
  gpio_set_level(GPIO_NUM_9, 1);
  // Rail an (GPIO38 LOW) - traegt die I2C-Pullups
  gpio_hold_dis(GPIO_NUM_38);
  gpio_set_direction(GPIO_NUM_38, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_38, 0);
  vTaskDelay(pdMS_TO_TICKS(8));
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = GPIO_NUM_20;
  bc.scl_io_num = GPIO_NUM_19;
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_new_master_bus(&bc, &bus) != ESP_OK) return -1;
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = 0x34;
  dc.scl_speed_hz = 400000;
  i2c_master_dev_handle_t dev = nullptr;
  int n = -1;
  if (i2c_master_bus_add_device(bus, &dc, &dev) == ESP_OK) {
    const int64_t ende = esp_timer_get_time() + (int64_t) warten_ms * 1000;
    bool gedrueckt = false;
    n = 0;
    for (;;) {
      uint8_t reg = 0x03, cnt = 0;
      if (i2c_master_transmit_receive(dev, &reg, 1, &cnt, 1, 50) != ESP_OK) { if (n == 0) n = -1; break; }
      cnt &= 0x0F;
      for (uint8_t i = 0; i < cnt && n < max; i++) {
        uint8_t r = 0x04, e = 0;
        if (i2c_master_transmit_receive(dev, &r, 1, &e, 1, 50) != ESP_OK) break;
        if ((e & 0x7F) != 0) {
          ev[n++] = e;
          if (e & 0x80) gedrueckt = true;
        }
      }
      if (cnt) {
        const uint8_t clr[2] = {0x02, 0x1F};   // Interrupt-Status loeschen (sonst bleibt INT unten)
        i2c_master_transmit(dev, clr, 2, 50);
      }
      if (gedrueckt || n >= max || stopp || esp_timer_get_time() >= ende) break;
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    i2c_master_bus_rm_device(dev);
  }
  i2c_del_master_bus(bus);
  return n;
}

// Weck-Tabelle: Pakete der Taste suchen (Format s. key_config::weck_tabelle_speichern)
static bool tabelle(uint8_t taste, std::vector<uint8_t> &blob, size_t &ab, uint8_t &npak) {
  nvs_handle_t h;
  if (nvs_open("tasten", NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = 0;
  bool ok = nvs_get_blob(h, "wtab", nullptr, &len) == ESP_OK && len >= 3 && len <= 16384;
  if (ok) {
    blob.resize(len);
    ok = nvs_get_blob(h, "wtab", blob.data(), &len) == ESP_OK;
  }
  nvs_close(h);
  if (!ok || blob[0] != 1) return false;
  size_t i = 3;
  for (int k = 0; k < blob[2] && i + 2 <= blob.size(); k++) {
    const uint8_t code = blob[i], n = blob[i + 1];
    size_t j = i + 2;
    for (int q = 0; q < n && j + 15 <= blob.size(); q++) j += 15 + (blob[j + 13] | (blob[j + 14] << 8));
    if (code == taste) {
      ab = i + 2;
      npak = n;
      return true;
    }
    i = j;
  }
  return false;
}

static void task(void *) {
  auto &d = esphome::dock_direkt::g_schnell;
  auto &f = esphome::tca8418::g_frueh_tasten;
  const bool bewegung = !(esp_sleep_get_ext1_wakeup_status() & (1ULL << 8));
  bericht = Bericht{BERICHT_MAGIC, 0, 0, 0, (uint8_t) (bewegung ? 1 : 0), 0};
  bool funk = false;
  if (bewegung) funk = funk_an(d.kanal);   // Funk vorbereiten, waehrend auf die Taste gewartet wird
  uint8_t ev[10];
  const int n = tasten_lesen(ev, 10, bewegung ? 3000 : 0);
  if (n > 0) {
    f.n = (uint8_t) n;
    memcpy(f.e, ev, n);
    f.gesendet = 0;
    f.magic = esphome::tca8418::FRUEH_MAGIC;   // Ereignisse gehoeren jetzt dem Tasten-Treiber
    uint8_t taste = 0;
    for (int i = 0; i < n; i++)
      if (ev[i] & 0x80) {
        taste = ev[i] & 0x7F;
        break;
      }
    bericht.taste = taste;
    nvs_flash_init();
    std::vector<uint8_t> blob;
    size_t ab = 0;
    uint8_t npak = 0;
    if (taste != 0 && tabelle(taste, blob, ab, npak) && npak > 0 && (funk || (funk = funk_an(d.kanal)))) {
      bool alle = true;
      size_t j = ab;
      for (uint8_t q = 0; q < npak && j + 15 <= blob.size(); q++) {
        const uint8_t *mac = &blob[j];
        const uint8_t typ = blob[j + 6];
        const uint32_t param = blob[j + 7] | (blob[j + 8] << 8) | (blob[j + 9] << 16) | ((uint32_t) blob[j + 10] << 24);
        const uint16_t anzahl = blob[j + 11] | (blob[j + 12] << 8);
        const uint16_t len = blob[j + 13] | (blob[j + 14] << 8);
        if (j + 15 + len > blob.size()) {
          alle = false;
          break;
        }
        const uint32_t nr = d.nr_frei + d.nr_benutzt;
        d.nr_benutzt++;
        if (!senden(mac, typ, param, anzahl, &blob[j + 15], len, nr)) {
          alle = false;
          break;
        }
        bericht.pakete++;
        j += 15 + len;
      }
      bericht.ok = alle ? 1 : 0;
      bericht.ack_ms = (uint16_t) (esp_timer_get_time() / 1000);
      // Nichts angekommen: der normale Weg sendet die Taste. Teilweise (2026-11-09, z. B. zweites Dock auf anderem
      // Kanal): NICHT nochmal - der normale Weg schickte die ganze Taste erneut, das erste Dock fuehrte sie doppelt aus
      // (Fernseher an und gleich wieder aus). Das unerreichte Dock verpasst dafuer diesen einen Druck.
      f.gesendet = (alle || bericht.pakete > 0) ? 1 : 0;
    }
  }
  if (funk) funk_aus();
  bootzeit_e = (uint32_t) (esp_timer_get_time() / 1000);
  laeuft = false;
  vTaskDelete(nullptr);
}

inline uint32_t bootzeit_loop = 0;   // ms: loopTask angelegt (= app_main fertig, danach baut setup() die Objekte)
static void loop_messen(void *) {
  for (int i = 0; i < 1500 && !bootzeit_loop; i++) {
    if (xTaskGetHandle("loopTask") != nullptr) bootzeit_loop = (uint32_t) (esp_timer_get_time() / 1000);
    else vTaskDelay(1);
  }
  vTaskDelete(nullptr);
}
__attribute__((constructor(103))) static void loop_messen_starten() {
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1)
    xTaskCreatePinnedToCore(loop_messen, "loop_messen", 2048, nullptr, 2, nullptr, 0);
}

__attribute__((constructor(102))) static void starten() {
  auto &d = esphome::dock_direkt::g_schnell;
  if (d.magic != esphome::dock_direkt::SCHNELL_MAGIC || d.kanal == 0 || d.nr_benutzt) return;
  // Ausgeschaltet oder Tiefentladeschutz (2026-11-09): nichts senden und den Tasten-FIFO nicht leeren - on_boot 950
  // sucht darin die Aus-Taste (102). Vorher leerte der Schnellweg ihn, die Remote liess sich nur noch per Kabel starten.
  if (g_ausgeschaltet == AUSGESCHALTET_MAGIC || g_akku_leer == AKKU_LEER_MAGIC) return;
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return;
  if (!(esp_sleep_get_ext1_wakeup_status() & ((1ULL << 8) | (1ULL << 2)))) return;   // Taste oder Bewegung
  laeuft = true;
  bootzeit_c = (uint32_t) (esp_timer_get_time() / 1000);
  // Kern 0: ESPHome (loopTask) laeuft auf Kern 1 (esphome/components/esp32/core.cpp) - so bremst der Funkstart hier
  // dessen Start nicht
  xTaskCreatePinnedToCore(task, "schnellweg", 6144, nullptr, 10, nullptr, 0);
}

// ESPHome: erste Komponente wartet, bis der Schnellweg Funk und I2C freigegeben hat (hoechstens 3,5 s)
inline void abwarten() {
  stopp = true;
  for (int i = 0; i < 3500 && laeuft; i++) vTaskDelay(pdMS_TO_TICKS(1));
}

// Fuer das Weckprotokoll nach dem Start
inline std::string text() {
  if (bericht.magic != BERICHT_MAGIC) return "";
  bericht.magic = 0;
  if (bericht.taste == 0) return std::string("Schnellweg: keine Taste (") + (bericht.bewegung ? "Bewegung" : "Taste") + ")";
  char b[96];
  snprintf(b, sizeof(b), "Schnellweg: Taste %u %s (%u Paket(e), Dock ok nach %u ms)", bericht.taste,
           bericht.ok ? "gesendet" : (bericht.pakete ? "teilweise" : "nicht in Weck-Tabelle - normal"), bericht.pakete,
           bericht.ack_ms);
  return b;
}

}  // namespace schnellweg

// Startzeiten (2026-11-06, Fehlersuche "Aufwachen dauert 3,5 s"): Marken je on_boot-Stufe, am Ende eine Zeile ins
// Weckprotokoll. ms seit dem Start der Firmware (esp_timer).
namespace bootzeit {
inline char buf[200];
inline int len = 0;
inline uint32_t hell_ms = 0;   // Display zum ersten Mal an (ms seit Start)
inline uint32_t erste = 0;
inline void mark(const char *n) {
  if (!erste) erste = (uint32_t) (esp_timer_get_time() / 1000);
  if (len >= (int) sizeof(buf) - 12) return;
  len += snprintf(buf + len, sizeof(buf) - len, "%s:%u ", n, (unsigned) (esp_timer_get_time() / 1000));
}
}  // namespace bootzeit
