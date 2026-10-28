#pragma once
// Protokoll-Puffer der Remote (2026-10-23 als Test, seit 2026-10-24 fest: Protokoll ueber das Dock). Merkt sich die
// letzten Zeilen ab INFO mit Zeitstempel im PSRAM - auch waehrend das WLAN aus ist (dann kommt kein Protokoll an).
// - Knopf "Protokoll-Puffer ausgeben": schreibt alles ins normale Protokoll (mit WLAN/HA-Verbindung).
// - Ohne eigene HA-Verbindung und mit "Debug-Protokoll" an: fuer_dock() liefert die noch nicht gesendeten Zeilen, die
//   Remote schickt sie jede Minute uebers Dock an HA (esphomeremote_konfig.remote_log, s. dock_status_melden).
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <cstring>
#include <cstdio>

namespace logpuffer {

static const int ZEILEN = 600;
static const int LAENGE = 160;
struct Zeile { uint32_t ms; char text[LAENGE]; };
static Zeile *g_z = nullptr;
static int g_kopf = 0, g_anzahl = 0;
static uint32_t g_nr = 0;        // Zeilen insgesamt (Zeile n liegt in g_z[(n - 1) % ZEILEN])
static uint32_t g_gesendet = 0;  // bis hierher ueber das Dock geschickt
static bool g_aus = false;   // waehrend der Ausgabe nicht mitschreiben
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

// aus logger.on_message (kann aus jedem Task kommen)
inline void merken(int level, const char *tag, const char *msg) {
  if (g_aus) return;
  // Bewegungs-Meldungen ("Ruck/Kippen erkannt", mehrmals je Sekunde) fuellten den Puffer in 2 min (2026-10-24)
  if (level >= 3 && strcmp(tag, "motion") == 0 && strstr(msg, " erkannt") != nullptr) return;
  // Bei jedem Funk-Ausleihen fuer das Dock (WLAN aus) - harmlos, kam alle 2 s und ging ueber das Dock an HA (2026-10-25)
  if (strcmp(tag, "wifi_esp32") == 0 && strstr(msg, "set_hostname failed") != nullptr) return;
  if (g_z == nullptr) {
    g_z = (Zeile *) heap_caps_calloc(ZEILEN, sizeof(Zeile), MALLOC_CAP_SPIRAM);
    if (g_z == nullptr) { g_aus = true; return; }
  }
  // Farbcodes und Kopf "[I][tag:123]: " ueberspringen - Tag schreiben wir selbst
  const char *p = strstr(msg, "]: ");
  p = p != nullptr ? p + 3 : msg;
  const char lv = level <= 1 ? 'E' : level == 2 ? 'W' : 'I';
  portENTER_CRITICAL(&g_mux);
  Zeile &z = g_z[g_kopf];
  z.ms = esphome::millis();
  snprintf(z.text, LAENGE, "%c %s: %s", lv, tag, p);
  g_kopf = (g_kopf + 1) % ZEILEN;
  g_nr++;
  if (g_anzahl < ZEILEN) g_anzahl++;
  portEXIT_CRITICAL(&g_mux);
}

// Noch nicht ueber das Dock geschickte Zeilen, hoechstens max Byte: je Zeile "<Alter in 0,1 s> <Text>\n".
// Merkt sie als gesendet - klappt das Senden nicht, mit zurueck(vorher) wieder freigeben.
inline std::string fuer_dock(size_t max) {
  std::string out;
  if (g_z == nullptr) return out;
  const uint32_t jetzt = esphome::millis();
  uint32_t n = g_gesendet + 1;
  if (g_nr >= (uint32_t) g_anzahl && n < g_nr - g_anzahl + 1) n = g_nr - g_anzahl + 1;   // aelteste sind ueberschrieben
  for (; n <= g_nr; n++) {
    char t[LAENGE + 12];
    portENTER_CRITICAL(&g_mux);
    const Zeile &z = g_z[(n - 1) % ZEILEN];
    int l = snprintf(t, sizeof(t), "%u ", (unsigned) ((jetzt - z.ms) / 100));
    strncpy(t + l, z.text, sizeof(t) - l - 1);
    portEXIT_CRITICAL(&g_mux);
    t[sizeof(t) - 1] = 0;
    // Farbcodes (ESC [ ... Buchstabe) entfernen, Zeilenumbrueche zu Leerzeichen
    char *o = t;
    for (const char *c = t; *c; c++) {
      if (*c == '\033') { if (c[1] == '[') { c += 2; while (*c && !((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z'))) c++; if (!*c) break; } continue; }
      *o++ = (*c == '\n' || *c == '\r') ? ' ' : *c;
    }
    *o = 0;
    while (o > t && o[-1] == ' ') *--o = 0;
    const size_t len = strlen(t);
    if (!out.empty() && out.size() + len + 1 > max) break;
    out.append(t, len > max ? max : len).push_back('\n');
    g_gesendet = n;
  }
  return out;
}
inline uint32_t gesendet() { return g_gesendet; }
inline void zurueck(uint32_t vorher) { if (vorher < g_gesendet) g_gesendet = vorher; }
inline void alles_gesendet() { g_gesendet = g_nr; }   // mit HA-Verbindung: kam ueber die API

inline void ausgeben() {
  if (g_z == nullptr || g_anzahl == 0) { ESP_LOGI("puffer", "Protokoll-Puffer leer"); return; }
  g_aus = true;
  const uint32_t jetzt = esphome::millis();
  const int start = (g_kopf - g_anzahl + ZEILEN) % ZEILEN;
  ESP_LOGI("puffer", "==== %d Zeilen, jetzt = %u ms ====", g_anzahl, (unsigned) jetzt);
  for (int i = 0; i < g_anzahl; i++) {
    const Zeile &z = g_z[(start + i) % ZEILEN];
    // Zeilenende/Farbreste abschneiden
    char t[LAENGE];
    strncpy(t, z.text, LAENGE);
    t[LAENGE - 1] = 0;
    for (char *c = t; *c; c++) if (*c == '\033') { *c = 0; break; }
    ESP_LOGI("puffer", "%8.3f s (vor %6.1f s) %s", z.ms / 1000.0f, (jetzt - z.ms) / 1000.0f, t);
    if ((i & 15) == 15) esphome::delay(20);   // API-Puffer nicht ueberlaufen lassen
  }
  ESP_LOGI("puffer", "==== Ende ====");   // Puffer bleibt (2026-10-24: Ausgeben bei zu leiser HA-Protokollstufe verlor ihn)
  g_aus = false;
}

}  // namespace logpuffer
