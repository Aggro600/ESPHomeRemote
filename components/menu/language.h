#pragma once
// Sprache auf dem Display (2026-11-01, Nutzer: "alles lokalisieren, nach der HA-Sprache, Fallback Englisch").
// Der Code bleibt deutsch; ist Englisch aktiv, ersetzt tr() bekannte Texte ueber die Tabelle (language_table.h,
// erzeugt von tools/language_gen.py). Feste Texte der Seiten uebersetzt anwenden() einmal beim Start (Baum aller
// Bildschirme), spaeter gesetzte Texte das Makro lv_label_set_text unten. Sprachwechsel = Neustart (remote.yaml).
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>
#include "esphome/core/hal.h"
#include "lvgl.h"
#include "language_table.h"

namespace sprache {

inline bool &englisch() { static bool e = false; return e; }

// Exakter Treffer (binaere Suche nach Bytes), sonst unveraendert
inline const char *tr(const char *s) {
  if (!englisch() || s == nullptr || *s == 0) return s;
  unsigned lo = 0, hi = TAB_EN_N;
  while (lo < hi) {
    const unsigned m = (lo + hi) / 2;
    const int c = strcmp(TAB_EN[m].de, s);
    if (c == 0) return TAB_EN[m].en;
    if (c < 0) lo = m + 1; else hi = m;
  }
  return s;
}
// Teilstuecke ersetzen (fuer Texte mit Zahlen dazwischen): laengste zuerst, nur an Wortgrenzen, ein Durchgang.
// Kleiner Zwischenspeicher, weil viele Texte immer wieder gleich gesetzt werden.
inline bool wortzeichen_(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80; }
inline std::string teile(const char *s) {
  struct E { std::string de, en; };
  static E cache[12];
  static uint8_t pos = 0;
  for (auto &e : cache) if (!e.de.empty() && e.de == s) return e.en;
  const size_t n = strlen(s);
  std::vector<int8_t> belegt(n, 0);
  struct T { size_t a, l; unsigned i; };
  std::vector<T> treffer;
  for (unsigned k = 0; k < TEIL_EN_N; k++) {
    const Paar &p = TAB_EN[TEIL_EN[k]];
    const size_t l = strlen(p.de);
    if (l > n) continue;
    for (const char *f = strstr(s, p.de); f != nullptr; f = strstr(f + 1, p.de)) {
      const size_t a = f - s;
      if ((a > 0 && wortzeichen_(s[a - 1]) && wortzeichen_(p.de[0])) || (a + l < n && wortzeichen_(s[a + l]) && wortzeichen_(p.de[l - 1]))) continue;
      bool frei = true;
      for (size_t j = a; j < a + l; j++) if (belegt[j]) { frei = false; break; }
      if (!frei) continue;
      for (size_t j = a; j < a + l; j++) belegt[j] = 1;
      treffer.push_back({a, l, TEIL_EN[k]});
    }
  }
  std::string out;
  if (!treffer.empty()) {
    std::sort(treffer.begin(), treffer.end(), [](const T &x, const T &y) { return x.a < y.a; });
    size_t i = 0;
    for (auto &t : treffer) { out.append(s + i, t.a - i); out += TAB_EN[t.i].en; i = t.a + t.l; }
    out.append(s + i);
  } else {
    out = s;
  }
  cache[pos] = {s, out};
  pos = (pos + 1) % 12;
  return out;
}

inline std::string tr(const std::string &s) { const char *t = tr(s.c_str()); return t == s.c_str() ? s : std::string(t); }



// Optionen von Dropdown/Roller: zeilenweise
inline std::string zeilen(const char *opts) {
  std::string out, z;
  for (const char *p = opts;; p++) {
    if (*p == '\n' || *p == 0) {
      const char *e = tr(z.c_str());
      out += (e == z.c_str() && z.size() >= 8) ? teile(z.c_str()) : std::string(e);
      if (*p == 0) break;
      out += '\n';
      z.clear();
    } else {
      z += *p;
    }
  }
  return out;
}

// Ganzer Text, sonst zeilenweise (Texte mit \n im Code stehen zeilenweise in der Tabelle)
inline void label_text(lv_obj_t *o, const char *t) {
  const char *n = tr(t);
  if (n == t && englisch() && t != nullptr && strlen(t) >= 8) {
    const std::string z = strchr(t, '\n') != nullptr ? zeilen(t) : teile(t);
    (lv_label_set_text)(o, z.c_str());
    return;
  }
  (lv_label_set_text)(o, n);
}

inline void baum_(lv_obj_t *o) {
  if (o == nullptr) return;
  esphome::arch_feed_wdt();   // ~2000 Tabelleneintraege je Text: der ganze Baum dauert laenger als der Watchdog erlaubt
  if (lv_obj_check_type(o, &lv_label_class)) {
    const char *t = lv_label_get_text(o);
    const char *n = tr(t);
    if (n != t) (lv_label_set_text)(o, n);
    else if (t != nullptr && strlen(t) >= 8) {
      const std::string z = strchr(t, '\n') != nullptr ? zeilen(t) : teile(t);
      if (z != t) (lv_label_set_text)(o, z.c_str());
    }
  }
#if LV_USE_DROPDOWN
  else if (lv_obj_check_type(o, &lv_dropdown_class)) {
    const char *t = lv_dropdown_get_options(o);
    if (t != nullptr) { std::string n = zeilen(t); if (n != t) lv_dropdown_set_options(o, n.c_str()); }
  }
#endif
#if LV_USE_ROLLER
  else if (lv_obj_check_type(o, &lv_roller_class)) {
    const char *t = lv_roller_get_options(o);
    if (t != nullptr) {
      std::string n = zeilen(t);
      if (n != t) { const uint32_t sel = lv_roller_get_selected(o); lv_roller_set_options(o, n.c_str(), LV_ROLLER_MODE_NORMAL); lv_roller_set_selected(o, sel, LV_ANIM_OFF); }
    }
  }
#endif
  const uint32_t k = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < k; i++) baum_(lv_obj_get_child(o, i));
}

// Alle Seiten (und die Ebene darueber) einmal uebersetzen
template<typename V> inline void anwenden(const V &seiten) {
  // Nur einmal: spaeter gesetzte Texte uebersetzt das Makro. Lief es bei jeder neuen Konfiguration, stand die
  // Hauptschleife jedes Mal Sekunden und der Watchdog startete die Remote neu (2026-11-02)
  static bool fertig = false;
  if (!englisch() || fertig) return;
  fertig = true;
  for (auto *s : seiten) baum_(s);
  baum_(lv_layer_top());
}

}  // namespace sprache

// Spaeter gesetzte Texte (Lambdas, eigene Header, Komponenten) gehen durch die Tabelle
#define lv_label_set_text(o, ...) ::sprache::label_text((o), __VA_ARGS__)
