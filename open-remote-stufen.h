// Wertetabellen der Stufen-Regler (Energie & Wecken, Bildschirm, Anzeige, Wartung).
// Der Regler steht auf dem INDEX, der gespeicherte Wert ist der Tabellenwert.
// Generiert/gepflegt zusammen mit remote-wz-sabrina.yaml (Stand 2026-09-28): die
// max_value der Slider im YAML muessen zur Tabellenlaenge passen (Anzahl - 1).
#pragma once
#include <cstdlib>

namespace stufen {

struct Tabelle { const int *v; int n; };

static const int DIM_PCT_V[] = {5, 10, 15, 20, 25, 30, 40, 50, 60, 70};
static const Tabelle DIM_PCT = {DIM_PCT_V, 10};
static const int WIFI_AUS_V[] = {0, 15, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200};
static const Tabelle WIFI_AUS = {WIFI_AUS_V, 17};
static const int TV_AUS_V[] = {-1, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200, 0};
static const Tabelle TV_AUS = {TV_AUS_V, 11};
static const int CPU_V[] = {0, 5, 10, 15, 20, 30, 45, 60, 120, 180, 300, -1};
static const Tabelle CPU = {CPU_V, 12};
static const int BLE_V[] = {5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600};
static const Tabelle BLE = {BLE_V, 12};
static const int TOUCH_MS_V[] = {50, 75, 100, 150, 200, 250, 300};
static const Tabelle TOUCH_MS = {TOUCH_MS_V, 7};
static const int TIEF_V[] = {30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800, 2700, 3600, 5400, 7200};
static const Tabelle TIEF = {TIEF_V, 15};
static const int AKKU_PCT_V[] = {0, 5, 10, 15, 20, 25, 30, 40};
static const Tabelle AKKU_PCT = {AKKU_PCT_V, 8};
static const int MERKEN_V[] = {5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, -1};
static const Tabelle MERKEN = {MERKEN_V, 12};
static const int SCHLIESSEN_V[] = {15, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1800, -1};
static const Tabelle SCHLIESSEN = {SCHLIESSEN_V, 13};
static const int REINIGEN_V[] = {10, 15, 20, 30, 45, 60, 90, 120};
static const Tabelle REINIGEN = {REINIGEN_V, 8};

inline int wert(const Tabelle &t, int i) {
  if (i < 0) i = 0;
  if (i >= t.n) i = t.n - 1;
  return t.v[i];
}

// Index zum gespeicherten Wert: exakt, sonst der naechstgelegene normale (> 0) Wert -
// so landen alte gespeicherte Werte, die es in der Tabelle nicht mehr gibt, sinnvoll.
inline int index(const Tabelle &t, int w) {
  for (int i = 0; i < t.n; i++) if (t.v[i] == w) return i;
  int best = 0, d = 0x7FFFFFFF;
  for (int i = 0; i < t.n; i++) {
    if (t.v[i] <= 0) continue;
    const int e = std::abs(t.v[i] - w);
    if (e < d) { d = e; best = i; }
  }
  return best;
}

}  // namespace stufen
