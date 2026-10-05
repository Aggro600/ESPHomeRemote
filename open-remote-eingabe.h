#pragma once
// Belegung der Bildschirm-Tastatur / des Ziffernblocks (Seite 41) und die
// D-Pad-Navigation darauf. Nur reine Daten und Rechnen mit ints - keine LVGL-Typen,
// damit die Datei unabhaengig von der Include-Reihenfolge in main.cpp ist.
// Modus 0 = Ziffernblock, Modus 1 = Tastatur (QWERTZ), Modus 1 + Umschalten = Grossbuchstaben.
#include <stdint.h>
#include <cmath>
#include <cstdio>

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

#include "lvgl.h"
// ---- Ziehen ist kein Tippen (2026-10-02) ------------------------------------------------------
// Auf Seiten, die nicht scrollen, startet LVGL beim Wischen kein Scrollen - das Loslassen loest dann
// den Knopf aus, auf dem der Finger begann. Hier: bewegt sich der Finger nach dem Aufsetzen um mehr
// als ZIEHEN_PX, wird der Druck abgebrochen (PRESS_LOST, kein CLICKED). Ausnahmen: Schieberegler
// (sollen gezogen werden), echtes Scrollen (macht LVGL selbst), und Seiten, die per Rueckruf
// ziehen_erlaubt() ausgenommen werden.
#define ZIEHEN_PX 15
// Aufruf aus touchscreen on_update (LVGL schickt PRESSING nicht an Eingabegeraete-Rueckrufe):
// dx/dy = Abstand vom Aufsetzpunkt. Bricht den Druck einmalig ab, wenn LVGL nicht selbst scrollt.
inline void ziehen_pruefen(int dx, int dy, bool ausgenommen, int x0, int y0) {
  static uint32_t abgebrochen_bei = 0;   // nur einmal je Beruehrung (Aufsetzpunkt als Kennung)
  const uint32_t kennung = ((uint32_t) (x0 & 0xFFFF) << 16) | (uint32_t) (y0 & 0xFFFF);
  if (ausgenommen || (LV_ABS(dx) < ZIEHEN_PX && LV_ABS(dy) < ZIEHEN_PX)) return;
  if (abgebrochen_bei == kennung) return;
  // Objekt unter dem AUFSETZPUNKT selbst suchen - lv_indev_get_active_obj() ist ausserhalb der
  // LVGL-Eingabeverarbeitung leer (daran scheiterte die erste Fassung auf der Menue-Startseite).
  lv_point_t p0{(int32_t) x0, (int32_t) y0};
  lv_obj_t *o = lv_indev_search_obj(lv_layer_top(), &p0);
  if (o == nullptr) o = lv_indev_search_obj(lv_screen_active(), &p0);
  for (lv_obj_t *t = o; t != nullptr; t = lv_obj_get_parent(t))
    if (lv_obj_check_type(t, &lv_slider_class) || lv_obj_check_type(t, &lv_bar_class)) return;   // Regler: ziehen erlaubt
  // Kann etwas unter dem Finger in Zugrichtung scrollen, gehoert die Geste LVGL - auch wenn es
  // noch nicht damit angefangen hat. on_update kommt oft VOR LVGLs eigener Auswertung; bei einem
  // schnellen Wisch war der Finger schon 15 px weit, bevor LVGL "scrollt" meldete, und der Abbruch
  // hat das Scrollen abgewuergt (Nutzerbefund 2026-10-03: nur langsames Ziehen scrollte).
  const bool senkrecht = LV_ABS(dy) >= LV_ABS(dx);
  for (lv_obj_t *t = o; t != nullptr; t = lv_obj_get_parent(t)) {
    if (!lv_obj_has_flag(t, LV_OBJ_FLAG_SCROLLABLE)) continue;
    if (senkrecht ? (lv_obj_get_scroll_top(t) > 0 || lv_obj_get_scroll_bottom(t) > 0)
                  : (lv_obj_get_scroll_left(t) > 0 || lv_obj_get_scroll_right(t) > 0))
      return;
  }
  for (lv_indev_t *in = lv_indev_get_next(nullptr); in != nullptr; in = lv_indev_get_next(in)) {
    if (lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER) continue;
    if (lv_indev_get_scroll_obj(in) != nullptr) continue;          // LVGL scrollt selbst -> kein Klick
    if (o != nullptr) lv_obj_remove_state(o, LV_STATE_PRESSED);
    lv_indev_wait_release(in);                                     // Loslassen: PRESS_LOST, kein CLICKED
    abgebrochen_bei = kennung;
  }
}

// Lade-Popup (2026-10-03): Karte auf lv_layer_top, von Script lade_popup angelegt und ausgeblendet.
inline lv_obj_t *&lade_karte() { static lv_obj_t *k = nullptr; return k; }
// Akkuanzeige (2026-10-04, Nutzerwunsch): kein "voll" mehr - 100 % nur, wenn der Akku wirklich voll
// geladen ist (voll = Ladeende erkannt, s. apply_charge_icon), sonst hoechstens 99 %.
inline int akku_anzeige_pct(float p, bool voll) {
  if (std::isnan(p)) return -1;
  int v = (int) p;
  if (voll) return 100;
  return v > 99 ? 99 : (v < 0 ? 0 : v);
}
// Text der Lade-Karte: "Akku 87 %", ohne Messwert "Akku wird gemessen" (statt "Akku ?").
inline void lade_popup_text(lv_obj_t *lbl, float p) {
  char b[28];
  if (std::isnan(p)) snprintf(b, sizeof(b), "Akku wird gemessen");
  else snprintf(b, sizeof(b), "Akku %d %%", akku_anzeige_pct(p, false));   // wie oben rechts; beim Laden nie 100
  lv_label_set_text(lbl, b);
}
