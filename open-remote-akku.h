// Akku-Statistik (Einstellungen > Akku, Stand 2026-09-28).
// Messpunkte (Uhrzeit + Ladestand) alle 5 min im RTC-Speicher. RTC_NOINIT ueberlebt Tiefschlaf,
// Neustart und OTA (nur nicht Stromlos-werden) - damit laeuft die Statistik auch ueber Schlaf-
// phasen durch, in denen die Remote nichts messen kann (der MAX17048 misst aber weiter).
// Beim Laden wird neu begonnen: die Raten sollen die ENTLADUNG beschreiben.
#pragma once
#include <cstring>
#include <cmath>
#include <string>
#include <ctime>
#include "esp_attr.h"

namespace akkustat {   // nicht "akku": so heisst schon der MAX17048-Sensor

struct Probe { uint32_t t; uint16_t pct10; uint16_t _r; };
static const int N = 300;                 // 300 x 5 min = 25 h
static const uint32_t MAGIC = 0xAC0A0002u;   // Aufbau geaendert -> neu anfangen
static const uint32_t ABSTAND = 300;      // s zwischen zwei Messpunkten

struct Speicher {
  uint32_t magic;
  uint16_t kopf, anzahl;                  // Ringpuffer: kopf = naechster Schreibplatz
  uint32_t geladen_t;                     // Ende der letzten Ladung (Unix-Zeit), 0 = unbekannt
  uint16_t geladen_pct10;
  uint16_t _r;
  uint32_t schlaf_start;                  // Beginn des laufenden Tiefschlafs (0 = keiner)
  uint32_t schlaf_summe;                  // Tiefschlaf-Sekunden seit `basis`
  uint32_t basis;                         // Beginn der Zaehlung = Ende der letzten Ladung (0 = unbekannt)
  Probe p[N];
};
RTC_NOINIT_ATTR Speicher g_akku;

inline void init() {
  if (g_akku.magic != MAGIC || g_akku.kopf >= N || g_akku.anzahl > N) {
    memset(&g_akku, 0, sizeof(g_akku));
    g_akku.magic = MAGIC;
  }
}
// Systemzeit laeuft im Tiefschlaf weiter (RTC) - deshalb ::time() statt ESPHome-Zeitkomponente,
// die kurz nach dem Aufwachen noch nicht eingerichtet ist.
inline uint32_t jetzt_s() {
  const time_t t = ::time(nullptr);
  return t > 1700000000 ? (uint32_t) t : 0;
}

inline const Probe &neueste() { return g_akku.p[(g_akku.kopf + N - 1) % N]; }

// Messpunkt, hoechstens alle ABSTAND Sekunden (erzwingen: vor dem Tiefschlaf).
inline void probe(uint32_t jetzt, float pct, bool erzwingen = false) {
  init();
  if (jetzt < 1700000000u || std::isnan(pct)) return;   // Uhr noch nicht gestellt
  if (g_akku.anzahl > 0) {
    const uint32_t alt = neueste().t;
    if (jetzt < alt) { g_akku.anzahl = 0; g_akku.kopf = 0; }       // Uhr sprang zurueck
    else if (!erzwingen && jetzt - alt < ABSTAND) return;
    else if (erzwingen && jetzt - alt < 30) return;
  }
  g_akku.p[g_akku.kopf] = {jetzt, (uint16_t) lroundf(pct * 10.0f), 0};
  g_akku.kopf = (g_akku.kopf + 1) % N;
  if (g_akku.anzahl < N) g_akku.anzahl++;
}

// Laden begonnen: alte Entlade-Messpunkte verwerfen. Laden beendet: Zeitpunkt merken.
inline void laden_start() {
  init();
  g_akku.anzahl = 0; g_akku.kopf = 0;
  g_akku.basis = 0; g_akku.schlaf_summe = 0;   // beim Laden zaehlt kein Tiefschlaf
}
inline void laden_ende(uint32_t jetzt, float pct) {
  init();
  if (jetzt < 1700000000u) return;
  g_akku.geladen_t = jetzt;
  g_akku.geladen_pct10 = std::isnan(pct) ? 0 : (uint16_t) lroundf(pct * 10.0f);
  g_akku.anzahl = 0; g_akku.kopf = 0;
  g_akku.basis = jetzt; g_akku.schlaf_summe = 0;
}

// Tiefschlaf-Zeit: vor dem Schlaf Beginn merken, beim Aufwachen aufsummieren.
inline void einschlafen() { init(); g_akku.schlaf_start = jetzt_s(); }
inline void aufgewacht() {
  init();
  const uint32_t j = jetzt_s();
  if (g_akku.schlaf_start && j > g_akku.schlaf_start && j - g_akku.schlaf_start < 90u * 86400u &&
      g_akku.basis && g_akku.schlaf_start >= g_akku.basis)
    g_akku.schlaf_summe += j - g_akku.schlaf_start;
  g_akku.schlaf_start = 0;
}

// Rate in %/h ueber ca. `fenster` Sekunden (negativ = Entladung). NAN, wenn weniger als
// `min_s` Sekunden Daten da sind. `spanne` = tatsaechlich genutzte Zeitspanne.
inline float rate(uint32_t jetzt, float pct, uint32_t fenster, uint32_t min_s, uint32_t &spanne) {
  init();
  spanne = 0;
  if (g_akku.anzahl == 0 || jetzt < 1700000000u || std::isnan(pct)) return NAN;
  // aeltester Punkt, der nicht aelter als das Fenster ist
  int best = -1;
  for (int i = 0; i < g_akku.anzahl; i++) {
    const Probe &q = g_akku.p[(g_akku.kopf + N - g_akku.anzahl + i) % N];
    if (q.t <= jetzt && jetzt - q.t <= fenster) { best = (g_akku.kopf + N - g_akku.anzahl + i) % N; break; }
  }
  if (best < 0) return NAN;
  const Probe &q = g_akku.p[best];
  spanne = jetzt - q.t;
  if (spanne < min_s) return NAN;
  return (pct - q.pct10 / 10.0f) / (spanne / 3600.0f);
}

inline std::string dauer(uint32_t s) {
  char b[24];
  if (s >= 86400) snprintf(b, sizeof(b), "%u T %u h", (unsigned) (s / 86400), (unsigned) ((s % 86400) / 3600));
  else if (s >= 3600) snprintf(b, sizeof(b), "%u h %u min", (unsigned) (s / 3600), (unsigned) ((s % 3600) / 60));
  else snprintf(b, sizeof(b), "%u min", (unsigned) (s / 60));
  return b;
}

}  // namespace akkustat
