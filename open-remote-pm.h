// Wird von ESPHome vor die erzeugten Lambdas eingebunden (esphome: includes:).
// esp_pm_configure() und esp_pm_config_t stehen sonst nicht zur Verfuegung,
// weil in einer Lambda kein #include moeglich ist.
#pragma once
#include "esp_pm.h"
// Fuer den Tiefschlaf: esp_sleep_get_wakeup_cause() / ESP_SLEEP_WAKEUP_TIMER,
// um nach dem Aufwachen zu erkennen, ob der Ruecksicherungs-Timer geweckt hat.
#include "esp_sleep.h"

// Ueberlebt den Deep-Sleep (RTC-Speicher), im Gegensatz zu den ESPHome-
// restore_value-Globals, die evtl. nicht rechtzeitig ins Flash geschrieben
// werden. 0x5EEE = "Tiefschlaf-Modus laeuft" -> nach jedem Wake ps_deep
// wieder auf true setzen, damit der Chip weiterschlaeft. Wird bei Power-On /
// Reset automatisch auf 0 genullt.
RTC_DATA_ATTR uint32_t g_deep_active;

// Fuer die Peripherie-Abschaltung im Deep-Sleep: gpio_hold_en / _dis,
// gpio_deep_sleep_hold_en, gpio_set_direction/level.
#include "driver/gpio.h"
// Fuer die ext1-Weckquellen (GPIO1 Ladekabel, GPIO2 Bewegung): interne
// RTC-Pull-ups scharf schalten, damit die Pins im Schlaf definiert HIGH sind.
#include "driver/rtc_io.h"

// ---- Herunterfahren (Stand 2026-09-28) --------------------------------------------------------
// Echtes Abschalten gibt die Hardware nicht her (Akku haengt fest am LDO). "Aus" = Tiefschlaf
// ohne Bewegungswecker; beim Aufwachen entscheidet on_boot (Prioritaet 950) anhand dieses Werts:
// nur die Aus-Taste (TCA8418-GPI, Code 102) oder das Ladekabel starten wirklich, jede andere Taste
// schickt das Geraet sofort wieder schlafen (Display bleibt dabei dunkel).
#define AUSGESCHALTET_MAGIC 0xA05Eu
RTC_DATA_ATTR uint32_t g_ausgeschaltet;

// Wieder einschlafen, ohne dass ESPHome die Peripherie hochgefahren hat. Entspricht Schritt 2-4
// von deep_sleep_powerdown, nur ohne LIS3DH (kein Bewegungswecker im Aus-Zustand).
inline void aus_wieder_schlafen() {
  const gpio_num_t lcd[] = {GPIO_NUM_39, GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42,
                            GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21, GPIO_NUM_14,
                            GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_11, GPIO_NUM_10};
  for (auto p : lcd) { gpio_set_direction(p, GPIO_MODE_OUTPUT); gpio_set_level(p, 0); }
  gpio_set_direction(GPIO_NUM_38, GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_38, 1);  // +VSW aus
  gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT);  gpio_set_level(GPIO_NUM_9, 1);   // Backlight aus
  gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_46, 0);  // Tastenlicht aus
  gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_16, 1);  // SD aus
  gpio_set_direction(GPIO_NUM_6, GPIO_MODE_OUTPUT);  gpio_set_level(GPIO_NUM_6, 0);   // IR-Empfaenger aus
  gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);  gpio_set_level(GPIO_NUM_5, 1);   // IR-LED aus
  gpio_reset_pin(GPIO_NUM_4);
  gpio_reset_pin(GPIO_NUM_19);
  gpio_reset_pin(GPIO_NUM_20);
  gpio_hold_en(GPIO_NUM_38);
  gpio_hold_en(GPIO_NUM_9);
  gpio_hold_en(GPIO_NUM_16);
  gpio_hold_en(GPIO_NUM_6);
  gpio_hold_en(GPIO_NUM_5);
  gpio_deep_sleep_hold_en();
  rtc_gpio_init(GPIO_NUM_1);
  rtc_gpio_set_direction(GPIO_NUM_1, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(GPIO_NUM_1);
  rtc_gpio_pullup_en(GPIO_NUM_1);
  rtc_gpio_hold_en(GPIO_NUM_1);
  uint64_t mask = (1ULL << 8);                          // Tasten (TCA8418-INT)
  if (rtc_gpio_get_level(GPIO_NUM_1)) mask |= (1ULL << 1);  // Ladekabel, nur wenn nicht schon aktiv
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

// ---- Tiefentladeschutz (2026-10-01) ----------------------------------------------------------
// Nils lief mit WLAN-Dauerbetrieb bis 2,56 V leer und startete per Brownout im Sekundentakt neu
// (Display dunkel, zuckend). Unter AKKU_LEER_V (zweimal hintereinander, ohne Ladekabel) schlaeft die
// Remote wie "Aus" (aus_wieder_schlafen: Wecken per Taste oder Ladekabel). Beim Aufwachen prueft
// on_boot 950 die Zellspannung direkt am MAX17048 und startet nur bei Kabel oder >= AKKU_WIEDER_V.
#define AKKU_LEER_MAGIC 0x1EE7u
#define AKKU_LEER_V 3.50f   // 2026-10-03: 3,30 zu tief - Brownout unter WLAN-Last schon bei ~3,4-3,5 V (Sabrina)
#define AKKU_WIEDER_V 3.70f
RTC_DATA_ATTR uint32_t g_akku_leer;

// ---- Weckprotokoll (Diagnose 2026-10-02) ----------------------------------------------------
// Merkt sich die letzten Weck-/Funkereignisse mit Uhrzeit (RTC-Speicher: ueberlebt Leicht- und
// Tiefschlaf, nicht das Abklemmen des Akkus). Abruf: HA-Sensor "Weckprotokoll" (letzte Eintraege)
// bzw. Dienst weckprotokoll_log (alles ins Log). Auch aus der Bluetooth-Komponente aufrufbar
// (dort als schwaches Symbol deklariert).
#include <ctime>
#define WECK_LOG_BYTES 1400
RTC_DATA_ATTR char g_weck_log[WECK_LOG_BYTES];
RTC_DATA_ATTR uint32_t g_weck_log_magic;
inline void weck_log_text_(const char *s) {
  if (g_weck_log_magic != 0x5EC0u) { g_weck_log[0] = 0; g_weck_log_magic = 0x5EC0u; }
  char z[24];
  time_t t = ::time(nullptr);
  if (t > 1700000000) { struct tm lt; localtime_r(&t, &lt); strftime(z, sizeof(z), "%d.%H:%M:%S", &lt); }
  else snprintf(z, sizeof(z), "+%lus", (unsigned long) (esp_timer_get_time() / 1000000));
  char e[96];
  snprintf(e, sizeof(e), "%s %s|", z, s);
  size_t lb = strlen(g_weck_log), le = strlen(e);
  while (lb + le >= WECK_LOG_BYTES - 1 && lb > 0) {        // aelteste Eintraege vorne verwerfen
    char *p = strchr(g_weck_log, '|');
    if (!p) { g_weck_log[0] = 0; lb = 0; break; }
    memmove(g_weck_log, p + 1, strlen(p + 1) + 1);
    lb = strlen(g_weck_log);
  }
  strcat(g_weck_log, e);
  ESP_LOGI("weck", "%s", s);
}
void weck_log(const char *s) { weck_log_text_(s); }
inline std::string weck_log_ende(size_t max) {
  std::string a(g_weck_log_magic == 0x5EC0u ? g_weck_log : "");
  return a.size() > max ? a.substr(a.size() - max) : a;
}
