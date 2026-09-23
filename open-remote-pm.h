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
