#pragma once
// Belegung der Bildschirm-Tastatur / des Ziffernblocks (Seite 41) und die
// D-Pad-Navigation darauf. Nur reine Daten und Rechnen mit ints - keine LVGL-Typen,
// damit die Datei unabhaengig von der Include-Reihenfolge in main.cpp ist.
// Modus 0 = Ziffernblock, Modus 1 = Tastatur (QWERTZ), Modus 1 + Umschalten = Grossbuchstaben.
#include <stdint.h>

namespace eingabe {

struct Layout {
  const char *const *map;   // LVGL-Tastenkarte ("\n" = neue Reihe, "" = Ende)
  const uint8_t *breite;    // relative Breite je Taste (nur echte Tasten, ohne "\n")
  const uint8_t *reihen;    // Tasten je Reihe
  int anzahl_reihen;
  int anzahl;               // Tasten insgesamt
  int start;                // Taste, auf der das D-Pad beginnt
};

static const char *const MAP_ZIFFERN[] = {
  "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", "<–", "0", "OK", ""};
static const uint8_t BR_ZIFFERN[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint8_t RE_ZIFFERN[4] = {3, 3, 3, 3};

static const char *const MAP_KLEIN[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
  "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\n",
  "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "\n",
  "^", "y", "x", "c", "v", "b", "n", "m", "<–", "\n",
  "ä", "ü", "Leer", ".", ",", "Ent", ""};
static const char *const MAP_GROSS[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
  "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\n",
  "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "\n",
  "^", "Y", "X", "C", "V", "B", "N", "M", "<–", "\n",
  "Ä", "Ü", "Leer", ".", ",", "Ent", ""};
// 10 Einheiten je Reihe: "<–" und "Ent" doppelt, "Leer" vierfach breit.
static const uint8_t BR_TASTATUR[45] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 2,
  1, 1, 4, 1, 1, 2};
static const uint8_t RE_TASTATUR[5] = {10, 10, 10, 9, 6};

inline const Layout &layout(int modus, bool gross) {
  static const Layout ziffern = {MAP_ZIFFERN, BR_ZIFFERN, RE_ZIFFERN, 4, 12, 4};
  static const Layout klein   = {MAP_KLEIN, BR_TASTATUR, RE_TASTATUR, 5, 45, 15};
  static const Layout grossl  = {MAP_GROSS, BR_TASTATUR, RE_TASTATUR, 5, 45, 15};
  return modus == 0 ? ziffern : (gross ? grossl : klein);
}

// Naechste Taste bei D-Pad-Druck. richtung: 0 hoch, 1 runter, 2 links, 3 rechts.
// Links/Rechts laufen innerhalb der Reihe im Kreis, Hoch/Runter wechseln die Reihe
// (auch im Kreis) und nehmen die Taste, die waagerecht am besten darunter/darueber liegt.
inline int weiter(const Layout &l, int sel, int richtung) {
  if (sel < 0 || sel >= l.anzahl) return l.start;
  int reihe = 0, erste = 0;
  while (reihe < l.anzahl_reihen - 1 && sel >= erste + l.reihen[reihe]) erste += l.reihen[reihe++];
  const int spalte = sel - erste;
  const int n = l.reihen[reihe];
  if (richtung == 2) return erste + (spalte + n - 1) % n;
  if (richtung == 3) return erste + (spalte + 1) % n;
  // Mitte der Taste in halben Einheiten
  int links = 0;
  for (int i = 0; i < spalte; i++) links += l.breite[erste + i];
  const int mitte2 = 2 * links + l.breite[sel];
  const int ziel = (richtung == 0) ? (reihe + l.anzahl_reihen - 1) % l.anzahl_reihen
                                   : (reihe + 1) % l.anzahl_reihen;
  int zerste = 0;
  for (int r = 0; r < ziel; r++) zerste += l.reihen[r];
  int pos = 0;
  for (int i = 0; i < l.reihen[ziel]; i++) {
    const int b = l.breite[zerste + i];
    if (mitte2 >= 2 * pos && mitte2 < 2 * (pos + b)) return zerste + i;
    pos += b;
  }
  return zerste + l.reihen[ziel] - 1;
}

}  // namespace eingabe
