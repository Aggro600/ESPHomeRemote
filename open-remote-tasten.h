#pragma once
// Musik-Anzeige (open-remote-musik.h, spaeter eingebunden)
namespace musik { inline void zeigen(bool vom_dock); }
#include <deque>
// Tasten-Konfigurator (2026-10-03): Bindeglied zwischen der Komponente tasten_konfig
// (Gesten, Laden aus HA, BLE/HA/IR) und den Funktionen der Remote (Skripte, Globals).
// KEINE ArduinoJson-Typen hier: main.cpp liegt an der l32r-Grenze (README, Lehren).
// Steht in esphome: includes: und damit in main.cpp HINTER allen Objekten - die
// YAML-ids sind hier als Zeiger sichtbar (kb, open_menu, activity ...).
//
// Aufruf aus der YAML:
//   on_boot (-100):  tasten::einrichten();
//   on_key:          if (tasten::ereignis(keycode, pressed)) return;   (nach Reinigung/Touch-Test)
//   Weck-Regel:      tasten::weckt(keycode)
//   text_sensor HA:  tk_cfg->angekuendigt(x);

namespace touchpad { inline void oeffnen(); inline bool taste(int k, bool gedrueckt); }   // open-remote-maus.h
namespace eingabewahl { inline void wechseln(int ziel); }   // open-remote-maus.h: 0 Maus, 1 Tastatur, 2 Ziffern

// Popup geht auf (2026-10-03): Herkunft merken, damit Schliessen dorthin zurueckfuehrt.
inline void popup_oeffnet(int seite) {
  const int p = cur_page->value();
  const bool schon_popup = popup_seite->value() >= 0 && p == popup_seite->value();
  if (!schon_popup) popup_von->value() = p;
  if (p == 98) mn->verlassen();   // konfigurierbares Menue: Stand fuer die Rueckkehr festhalten
  popup_seite->value() = seite;
  popup_min->value() = -1;
}

namespace tasten {
using namespace esphome;
using tasten_konfig::Ctx;

// Diese Tasten gehoeren auf den Seiten der Remote der Navigation (dispatch_key).
inline bool nav_taste(int k) { return k == 14 || k == 35 || k == 15 || k == 32 || k == 34 || k == 13; }

// ---- Funktionen der Remote (Schritte "int" der Belegung) ----
inline bool begruessung_sperrt(bool dunkel) { return cur_page->value() == 24 && (dunkel || millis() < 6000); }

// Weggelegtes Popup noch zurueckholbar? Nur solange sein Anlass besteht, hoechstens 5 min.
inline bool popup_zurueckholbar() {
  const int m = popup_min->value();
  if (m < 0 || millis() - popup_min_ms->value() > 300000) return false;
  if (m == 41) return kbd_streamer_offen->value();   // Tastatur: solange sie am Streamer offen ist
  if (m == 51) return true;                          // Touchpad (2026-10-17)
  return m == 98;                                     // Menue-Seite als Popup (z. B. Klingel, Kameras)
}

// Menuetaste kurz (2026-10-03): offenes Popup weglegen, weggelegtes zurueckholen,
// sonst Menue <-> Startseite.
inline void menue_kurz(bool dunkel) {
  if (begruessung_sperrt(dunkel)) return;
  const int p = cur_page->value();
  if (popup_seite->value() >= 0 && p == popup_seite->value()) {
    if (p == 41) { kbd_popup_zu->value() = true; kbd_popup->value() = false; }
    go_home->execute();   // go_home legt das Popup als "weggelegt" ab (popup_min)
    return;
  }
  if (p == 0 && popup_zurueckholbar()) { popup_wieder->execute(); return; }
  if (p != 0) go_home->execute();
  else open_menu->execute();
}

// Menue-Seite als Popup (HA-Dienst menue_popup): wie die eingebauten Popups - Schliessen fuehrt
// zurueck, Menuetaste kurz legt weg und holt wieder.
inline void menue_popup(const std::string &seite, bool wecken = true) {
  if (!mn->hat_seite(seite)) { ESP_LOGW("menue", "Popup-Seite '%s' unbekannt", seite.c_str()); return; }
  // Schon offen (z. B. im Kamera-Popup eine andere Kamera gewaehlt): nur wecken, Fokus behalten
  if (popup_seite->value() == 98 && cur_page->value() == 98 && mn->ist_popup() && mn->popup_seite() == seite) {
    if (wecken) wake_backlight->execute();
    return;
  }
  popup_oeffnet(98);
  mn->popup(seite);
  if (wecken) wake_backlight->execute();
  page_konfig->get_parent()->show_page(page_konfig->index, LV_SCR_LOAD_ANIM_FADE_IN, 150);
}
inline void menue_popup_zu() {
  if (popup_seite->value() == 98 && cur_page->value() == 98) popup_schliessen->execute();
  else if (popup_min->value() == 98) popup_min->value() = -1;
}

// ---- Aktivitaeten aus der Konfiguration (2026-10-04): Anzahl/Namen/Bluetooth-Platz frei, 1-4 ----
inline int aktivitaeten_anzahl() { const int n = tk_cfg->aktivitaeten(); return n > 0 ? n : 1; }
inline int aktivitaet_slot(int a) {
  if (tk_cfg->aktivitaeten() <= 0) return a;   // ohne Konfiguration: Aktivitaet = Platz
  return tk_cfg->aktivitaet_slot(a);
}

// Aktivitaet, die diesen Bluetooth-Platz nutzt (erste), -1 = keine
inline int aktivitaet_von_platz(int platz) {
  for (int a = 0; a < aktivitaeten_anzahl(); a++) if (aktivitaet_slot(a) == platz) return a;
  return -1;
}

// Aktivitaet starten (2026-10-16, wie Harmony): laeuft eine andere, fuehrt sie erst "Beim Beenden" aus
// (Pseudo-Taste 901), dann die neue "Beim Starten" (900). akt_an = false: keine Aktivitaet laeuft.
inline void aktivitaet_setzen(int a) {
  if (a < 0 || a >= aktivitaeten_anzahl()) return;
  const int alt = activity->value();
  const bool start = !akt_an->value() || alt != a;
  if (akt_an->value() && alt != a) tk_cfg->aktivitaet_ereignis(alt, false);
  activity->value() = a;
  akt_an->value() = true;
  apply_activity->execute();
  if (start) tk_cfg->aktivitaet_ereignis(a, true);
  mn->ereignis("aktivitaet", tk_cfg->aktivitaet_name(a));   // Popup-Ausloeser (z. B. "TV auf HDMI umschalten?")
}
// Laufende Aktivitaet beenden (HA-Schalter aus, Funktion "activity_off"); die Tastenbelegung bleibt.
inline void aktivitaet_aus() {
  if (!akt_an->value()) return;
  tk_cfg->aktivitaet_ereignis(activity->value(), false);
  akt_an->value() = false;
  apply_activity->execute();
}

// Aktivitaetsleiste der Startseite aus der Konfiguration bauen und die aktive markieren.
struct Dock { std::vector<lv_obj_t *> knoepfe; std::vector<int> akt; std::string sig; int slot = -1; lv_obj_t *ecke[2] = {nullptr, nullptr}; };
inline Dock &dock() { static Dock d; return d; }
// Abgerundete Ecke (LCARS) am Nachbarfeld des aktiven Felds: blaues 10x10-Quadrat ueber der Fuge, darin ein
// Kreis in Feldfarbe - ergibt die runde obere Ecke des Nachbarn. links=true: Ecke rechts oben am linken Nachbarn.
inline void dock_ecke(lv_obj_t *&e, bool links) {
  if (e != nullptr) return;
  e = lv_obj_create(lv_obj_get_parent(home_dock));
  lv_obj_set_size(e, 10, 10);
  lv_obj_set_style_radius(e, 0, 0);
  lv_obj_set_style_bg_color(e, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(e, 0, 0);
  lv_obj_set_style_pad_all(e, 0, 0);
  lv_obj_remove_flag(e, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_add_flag(e, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_t *k = lv_obj_create(e);
  lv_obj_set_size(k, 20, 20);
  lv_obj_set_pos(k, links ? -10 : 0, 0);
  lv_obj_set_style_radius(k, 10, 0);
  lv_obj_set_style_bg_color(k, lv_color_hex(0x1A1F27), 0);
  lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(k, 0, 0);
  lv_obj_remove_flag(k, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
}
// Kopfzeile im Dock-Stil (2026-10-13, Nutzerwunsch): unten eine durchgehende blaue Linie (3 px wie das Dock),
// der Zurueck-Knopf sitzt in einem blauen Feld, das bis an den linken und oberen Displayrand reicht und
// rechts ueber eine ausgerundete Ecke in die Linie uebergeht. hinter = Hintergrundfarbe der Kopfzeile.
// Einmal pro Kopfzeile; hd ist ein Flex-Behaelter, die Zierde liegt ausserhalb des Layouts.
struct KopfZier { lv_obj_t *hd, *btn, *linie, *feld, *ecke; int16_t lx, lw, fy, fh, ex, ey, eo; };
inline std::deque<KopfZier> &kopf_ziere() { static std::deque<KopfZier> v; return v; }   // deque: Zeiger bleiben gueltig (Animation)
inline void kopf_linie(lv_obj_t *hd, lv_obj_t *btn, uint32_t hinter = 0x0E1116) {
  static const uint32_t BLAU = 0x3B82F6;
  static const int DICK = 3, R = 10;
  if (hd == nullptr || btn == nullptr || lv_obj_get_user_data(hd) == (void *) 0x4B4C) return;
  lv_obj_set_user_data(hd, (void *) 0x4B4C);
  lv_obj_set_style_pad_bottom(hd, DICK, 0);
  lv_obj_update_layout(hd);
  lv_area_t k, b, in;
  lv_obj_get_coords(hd, &k);
  // Knopf mittig im blauen Feld (2026-10-13): oben, links, rechts und unten (zur Linie) je RAND Abstand,
  // damit der weisse Fokusrahmen gleichmaessig im Feld sitzt. Verschoben per translate (Titel bleibt stehen).
  static const int RAND = 4;
  lv_obj_set_height(btn, (k.y2 - DICK + 1) - k.y1 - 2 * RAND);
  lv_obj_update_layout(hd);
  lv_obj_get_coords(btn, &b);
  const int dx = (k.x1 + RAND) - b.x1, dy = (k.y1 + RAND) - b.y1;
  lv_obj_set_style_translate_x(btn, dx, 0);
  lv_obj_set_style_translate_y(btn, dy, 0);
  b.x1 += dx; b.x2 += dx; b.y1 += dy; b.y2 += dy;
  // Titel & Co. auf Knopfmitte (Hoehe), sonst sitzen sie nach dem Verschieben tiefer als der Knopf
  for (uint32_t i = 0; i < lv_obj_get_child_count(hd); i++) {
    lv_obj_t *c = lv_obj_get_child(hd, i);
    if (c == btn) continue;
    lv_area_t a;
    lv_obj_get_coords(c, &a);
    lv_obj_set_style_translate_y(c, ((b.y1 + b.y2) - (a.y1 + a.y2)) / 2, 0);
  }
  b.x2 += RAND;   // ab hier: rechter Rand des Feldes
  lv_obj_get_content_coords(hd, &in);
  // absolute Displaykoordinaten -> Position im Inhaltsbereich von hd
  auto teil = [&](int x, int y, int w, int hh, uint32_t farbe) {
    lv_obj_t *o = lv_obj_create(hd);
    lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(o, w, hh);
    lv_obj_set_pos(o, x - in.x1, y - in.y1);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(farbe), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_move_background(o);   // hinter Knopf und Titel; die Reihenfolge im Flex bleibt (IGNORE_LAYOUT)
    return o;
  };
  const int linie = k.y2 - DICK + 1;
  KopfZier z{};
  z.hd = hd; z.btn = btn;
  z.linie = teil(k.x1, linie, k.x2 - k.x1 + 1, DICK, BLAU);              // Linie ueber die ganze Breite
  // Feld: links und oben bis zum Rand, oben rechts abgerundet. Die anderen drei Ecken liegen ausserhalb
  // der Kopfzeile (links/unten ueberstehend) und werden abgeschnitten.
  z.feld = teil(k.x1 - R, k.y1, b.x2 - k.x1 + 1 + R, k.y2 - k.y1 + 1 + R, BLAU);
  lv_obj_set_style_radius(z.feld, R, 0);
  z.ecke = teil(b.x2 + 1, linie - R, R, R, BLAU);                         // Ausrundung rechts in die Linie
  lv_obj_t *kreis = lv_obj_create(z.ecke);
  lv_obj_remove_flag(kreis, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_size(kreis, 2 * R, 2 * R);
  lv_obj_set_pos(kreis, 0, -R);
  lv_obj_set_style_radius(kreis, R, 0);
  lv_obj_set_style_bg_color(kreis, lv_color_hex(hinter), 0);
  lv_obj_set_style_bg_opa(kreis, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(kreis, 0, 0);
  // Masse direkt merken: lv_obj_get_x/width liefern erst nach dem naechsten Layoutlauf die neuen Werte
  z.lx = k.x1 - in.x1;      z.lw = k.x2 - k.x1 + 1;
  z.fy = k.y1 - in.y1;      z.fh = k.y2 - k.y1 + 1 + R;
  z.ex = b.x2 + 1 - in.x1;
  z.ey = linie - R - in.y1;   // Ecke: oberer Rand
  z.eo = R + DICK;            // Fuellhoehe, ab der das Feld ueber der Linie ankommt (Feld steht R unter dem Kopf ueber)
  kopf_ziere().push_back(z);
}
// Aufbau-Animation beim Oeffnen des Menues (2026-10-13, Nutzerwunsch): die Linie waechst von rechts nach
// links, erreicht sie den Zurueck-Knopf, fuellt sich sein Feld von unten nach oben blau.
// verzoegerung = Dauer der Seiten-Einblendung, die Animation folgt danach.
inline void kopf_anim(lv_obj_t *hd, lv_obj_t *btn, uint32_t verzoegerung = 150) {
  const uint32_t T_LINIE = 280 * mn->zeitlupe, T_FELD = 160 * mn->zeitlupe;   // zeitlupe: nur fuer Bildschirmfotos
  kopf_linie(hd, btn);
  KopfZier *zp = nullptr;
  for (auto &z : kopf_ziere()) if (z.hd == hd) zp = &z;
  if (zp == nullptr) return;
  KopfZier &z = *zp;
  // Startbild: Linie leer, Feld und Ecke leer, Knopf ohne Farbe (nur das X)
  lv_anim_delete(&z, nullptr);
  lv_obj_set_width(z.linie, 0);
  lv_obj_set_x(z.linie, z.lx + z.lw);
  lv_obj_set_height(z.feld, 0);
  lv_obj_set_y(z.feld, z.fy + z.fh);
  lv_obj_add_flag(z.ecke, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(z.btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(z.btn, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &z);
  lv_anim_set_delay(&a, verzoegerung);
  lv_anim_set_duration(&a, T_LINIE);
  lv_anim_set_values(&a, 0, z.lw);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t w) {
    auto *z = (KopfZier *) v;
    lv_obj_set_width(z->linie, w);
    lv_obj_set_x(z->linie, z->lx + z->lw - w);
  });
  lv_anim_start(&a);
  lv_anim_t f;
  lv_anim_init(&f);
  lv_anim_set_var(&f, &z);
  // Das Feld startet, wenn die Linie ganz durch ist (Nutzerwunsch 2026-10-13)
  lv_anim_set_delay(&f, verzoegerung + T_LINIE);
  lv_anim_set_duration(&f, T_FELD);
  lv_anim_set_values(&f, 0, z.fh);
  lv_anim_set_path_cb(&f, lv_anim_path_ease_out);
  lv_anim_set_early_apply(&f, false);
  lv_anim_set_exec_cb(&f, [](void *v, int32_t h) {
    auto *z = (KopfZier *) v;
    lv_obj_set_height(z->feld, h);
    lv_obj_set_y(z->feld, z->fy + z->fh - h);
    // Ausrundung waechst mit dem Feld von der Linie nach oben (nicht schlagartig ganz blau)
    static const int R = 10;
    const int eh = std::max(0, std::min<int>(R, h - z->eo));
    if (eh == 0) { lv_obj_add_flag(z->ecke, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(z->ecke, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(z->ecke, z->ey + R - eh);
    lv_obj_set_height(z->ecke, eh);
    lv_obj_set_y(lv_obj_get_child(z->ecke, 0), eh - 2 * R);   // Kreis bleibt an seiner Stelle
  });
  lv_anim_set_completed_cb(&f, [](lv_anim_t *an) {
    auto *z = (KopfZier *) an->var;
    lv_obj_set_style_bg_opa(z->btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(z->btn, LV_OPA_COVER, 0);
  });
  lv_anim_start(&f);
}
inline void dock_anim_aufraeumen();   // unten (Dock-Animation)
inline void dock_anwenden(int a) {
  // Im Dock: die als "dock" markierten Aktivitaeten (hoechstens 4), der Rest nur ueber Menue/Tasten
  std::vector<int> akt;
  for (int i = 0; i < tk_cfg->aktivitaeten() && akt.size() < 4; i++)
    if (tk_cfg->aktivitaet_im_dock(i)) akt.push_back(i);
  const int n = (int) akt.size();
  Dock &D = dock();
  std::string sig;
  for (int i : akt) sig += std::to_string(i) + ":" + tk_cfg->aktivitaet_name(i) + "|";
  const int W = lv_obj_get_width(home_dock);
  sig += std::to_string(W);
  if (sig != D.sig) {
    dock_anim_aufraeumen();   // Fuellung haengt an einem Knopf, der gleich geloescht wird
    // Felder nebeneinander, 2 px Fuge (im YAML geht ein Flex-Layout ohne feste Kinder nicht)
    lv_obj_set_flex_flow(home_dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(home_dock, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(home_dock, 2, 0);
    for (auto *k : D.knoepfe) lv_obj_delete(k);
    D.knoepfe.clear();
    // Gleiche Breiten mit 2 px Fuge; der Rest geht an das mittlere Feld, damit die Leiste genau ausgefuellt
    // ist und die Ecken auf den Fugen sitzen.
    const int summe = W - 2 * (std::max(n, 1) - 1), basis = n ? summe / n : 0, rest = n ? summe % n : 0;
    for (int i = 0; i < n; i++) {
      const int breite = basis + (i == n / 2 ? rest : 0);
      lv_obj_t *k = lv_button_create(home_dock);
      lv_obj_set_size(k, breite, LV_PCT(100));
      lv_obj_set_style_radius(k, 0, 0);
      lv_obj_set_style_border_width(k, 0, 0);
      lv_obj_set_style_shadow_width(k, 0, 0);
      lv_obj_t *l = lv_label_create(k);
      lv_label_set_text(l, tk_cfg->aktivitaet_name(akt[i]).c_str());
      lv_obj_set_style_text_font(l, a13->get_lv_font(), 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
      lv_obj_set_width(l, breite - 6);
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_center(l);
      lv_obj_add_event_cb(k, [](lv_event_t *e) { aktivitaet_setzen((int) (intptr_t) lv_event_get_user_data(e)); },
                          LV_EVENT_CLICKED, (void *) (intptr_t) akt[i]);
      D.knoepfe.push_back(k);
    }
    D.akt = akt;
    D.sig = sig;
  }
  int pos = -1;   // Position der aktiven Aktivitaet im Dock (-1 = nicht im Dock)
  for (int i = 0; i < n; i++) {
    const bool an = akt_an->value() && D.akt[i] == a;   // beendet: nichts markiert
    if (an) pos = i;
    lv_obj_set_style_bg_color(D.knoepfe[i], lv_color_hex(an ? 0x3B82F6 : 0x1A1F27), 0);
    lv_obj_set_style_text_color(D.knoepfe[i], lv_color_hex(an ? 0xF1F3F5 : 0x8B94A3), 0);
  }
  D.slot = aktivitaet_slot(a);   // Geraet merken: bleibt aktiv, auch wenn die Reihenfolge sich aendert
  // Runde Ecken an den Nachbarn des aktiven Felds (fuer 2, 3 und 4 Felder, auch bei verschobenem Dock)
  dock_ecke(D.ecke[0], true);
  dock_ecke(D.ecke[1], false);
  for (auto *e : D.ecke) lv_obj_add_flag(e, LV_OBJ_FLAG_HIDDEN);
  if (pos >= 0 && !lv_obj_has_flag(home_dock, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_update_layout(home_dock);
    lv_obj_t *seite = lv_obj_get_parent(home_dock);
    lv_area_t s, k;
    lv_obj_get_content_coords(seite, &s);
    lv_obj_get_coords(D.knoepfe[pos], &k);
    const int y = k.y1 - s.y1;
    if (pos > 0) {   // linker Nachbar: Ecke endet am Anfang des aktiven Felds (deckt die Fuge)
      lv_obj_set_pos(D.ecke[0], k.x1 - 10 - s.x1, y);
      lv_obj_remove_flag(D.ecke[0], LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(D.ecke[0]);
    }
    if (pos < n - 1) {   // rechter Nachbar: Ecke beginnt am Ende des aktiven Felds
      lv_obj_set_pos(D.ecke[1], k.x2 + 1 - s.x1, y);
      lv_obj_remove_flag(D.ecke[1], LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(D.ecke[1]);
    }
  }
}

// Aufbau-Animation des Docks beim Erscheinen der Startseite (2026-10-13, Nutzerwunsch, wie die Menue-Kopfzeile):
// die blaue Linie oben am Dock waechst von beiden Displayraendern zum aktiven Feld - beide Seiten kommen
// gleichzeitig an (je nach Strecke verschieden schnell; liegt das Feld am Rand, nur von einer Seite). Dann
// fuellt sich das aktive Feld von oben nach unten blau. Am Ende stellt dock_anwenden den Normalzustand her.
struct DockAnim { lv_obj_t *li{nullptr}, *re{nullptr}, *oben{nullptr}, *fuell{nullptr}, *knopf{nullptr};
                  int16_t lx, lw, rx, rw, fh; uint8_t ecken{0}; };
inline DockAnim &dock_anim_zustand() { static DockAnim a; return a; }
inline void dock_anim_aufraeumen() {
  DockAnim &A = dock_anim_zustand();
  lv_anim_delete(&A, nullptr);
  for (lv_obj_t **o : {&A.li, &A.re, &A.oben, &A.fuell})
    if (*o) { lv_obj_delete(*o); *o = nullptr; }
  A.knopf = nullptr;
  lv_obj_remove_local_style_prop(home_dock, LV_STYLE_BORDER_OPA, 0);
  for (auto *e : dock().ecke) if (e) lv_obj_set_height(e, 10);
}
// verz: Wartezeit in ms vor dem Start (Seitenwechsel/Display an: sonst ist alles vorbei, bevor man hinsieht)
inline void dock_anim(uint32_t verz = 0) {
  const uint32_t T_LINIE = 300 * mn->zeitlupe, T_FUELL = 180 * mn->zeitlupe;
  static const uint32_t BLAU = 0x3B82F6;
  dock_anim_aufraeumen();
  Dock &D = dock();
  if (lv_obj_has_flag(home_dock, LV_OBJ_FLAG_HIDDEN) || D.knoepfe.empty()) return;
  int pos = -1;
  for (size_t i = 0; i < D.akt.size(); i++) if (D.akt[i] == activity->value()) pos = (int) i;
  if (pos < 0 || !akt_an->value()) return;   // keine Aktivitaet an: kein Feld markieren (sonst kurz blau, dann zurueck)
  DockAnim &A = dock_anim_zustand();
  lv_obj_update_layout(home_dock);
  lv_obj_t *seite = lv_obj_get_parent(home_dock);
  lv_area_t s, d, k;
  lv_obj_get_content_coords(seite, &s);
  lv_obj_get_coords(home_dock, &d);
  lv_obj_get_coords(D.knoepfe[pos], &k);
  const int dick = lv_obj_get_style_border_width(home_dock, LV_PART_MAIN);
  auto teil = [&](lv_obj_t *eltern, int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(eltern);
    lv_obj_remove_style_all(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(o, lv_color_hex(BLAU), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
  };
  // Linien in Seitenkoordinaten ueber dem (ausgeblendeten) Dockrand
  A.lx = d.x1 - s.x1; A.lw = k.x1 - d.x1;
  A.rx = k.x2 + 1 - s.x1; A.rw = d.x2 - k.x2;
  A.li = teil(seite, A.lx, d.y1 - s.y1, 0, dick);
  A.re = teil(seite, A.rx + A.rw, d.y1 - s.y1, 0, dick);
  A.oben = teil(seite, k.x1 - s.x1, d.y1 - s.y1, k.x2 - k.x1 + 1, dick);   // Rand ueber dem aktiven Feld
  lv_obj_add_flag(A.oben, LV_OBJ_FLAG_HIDDEN);
  // Fuellung im aktiven Feld, hinter der Beschriftung; das Feld selbst vorerst dunkel
  A.knopf = D.knoepfe[pos];
  A.fh = k.y2 - k.y1 + 1;
  // Kinder liegen im Inhaltsbereich: um den Innenabstand des Knopfs nach links/oben versetzen
  A.fuell = teil(A.knopf, -lv_obj_get_style_pad_left(A.knopf, LV_PART_MAIN), -lv_obj_get_style_pad_top(A.knopf, LV_PART_MAIN),
                 k.x2 - k.x1 + 1, 0);
  lv_obj_move_background(A.fuell);
  lv_obj_set_style_bg_color(A.knopf, lv_color_hex(0x1A1F27), 0);
  lv_obj_set_style_border_opa(home_dock, LV_OPA_TRANSP, 0);
  A.ecken = 0;   // welche Nachbar-Ecken dock_anwenden gezeigt hat: kommen mit der Fuellung wieder
  for (int i = 0; i < 2; i++)
    if (D.ecke[i] && !lv_obj_has_flag(D.ecke[i], LV_OBJ_FLAG_HIDDEN)) { A.ecken |= 1 << i; lv_obj_add_flag(D.ecke[i], LV_OBJ_FLAG_HIDDEN); }
  // Linien: Fortschritt 0..1024, beide Seiten erreichen das Feld zur selben Zeit
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &A);
  lv_anim_set_delay(&a, verz);   // early_apply: Linien sofort auf 0
  lv_anim_set_duration(&a, T_LINIE);
  lv_anim_set_values(&a, 0, 1024);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, [](void *v, int32_t p) {
    auto *A = (DockAnim *) v;
    const int wl = A->lw * p / 1024, wr = A->rw * p / 1024;
    lv_obj_set_width(A->li, wl);
    lv_obj_set_width(A->re, wr);
    lv_obj_set_x(A->re, A->rx + A->rw - wr);
  });
  lv_anim_start(&a);
  // Feld von oben nach unten fuellen
  lv_anim_t f;
  lv_anim_init(&f);
  lv_anim_set_var(&f, &A);
  lv_anim_set_delay(&f, verz + T_LINIE);
  lv_anim_set_early_apply(&f, false);
  lv_anim_set_duration(&f, T_FUELL);
  lv_anim_set_values(&f, 0, A.fh);
  lv_anim_set_path_cb(&f, lv_anim_path_ease_out);
  lv_anim_set_start_cb(&f, [](lv_anim_t *an) {
    auto *A = (DockAnim *) an->var;
    lv_obj_remove_flag(A->oben, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++) if ((A->ecken & (1 << i)) && dock().ecke[i]) lv_obj_remove_flag(dock().ecke[i], LV_OBJ_FLAG_HIDDEN);
  });
  lv_anim_set_exec_cb(&f, [](void *v, int32_t h) {
    auto *A = (DockAnim *) v;
    lv_obj_set_height(A->fuell, h);
    // runde Ecken der Nachbarn wachsen mit der Fuellung nach unten (nicht schlagartig ganz blau)
    for (int i = 0; i < 2; i++)
      if ((A->ecken & (1 << i)) && dock().ecke[i]) lv_obj_set_height(dock().ecke[i], std::max<int>(1, std::min<int>(10, h)));
  });
  lv_anim_set_completed_cb(&f, [](lv_anim_t *) {
    // Nicht im Ablauf der Animation loeschen (lv_anim_delete auf die laufende): erst im naechsten Durchlauf
    lv_async_call([](void *) { dock_anim_aufraeumen(); dock_anwenden(activity->value()); }, nullptr);
  });
  lv_anim_start(&f);
}

inline void seite_zeigen(lvgl::LvPageType *seite, lv_screen_load_anim_t anim);
inline void intern(const char *fn, int arg, const Ctx &c) {
  const bool d = c.dunkel;
  // Einstellung "Menütaste bei dunklem Display: Nur Display" (2026-10-11): der weckende Druck oeffnet kein Menue
  if (d && menue_weckt_nur->value() && (!strcmp(fn, "menu_smart") || !strcmp(fn, "menu_open") || !strcmp(fn, "menu_top")))
    return;
  if (!strcmp(fn, "menu_smart")) menue_kurz(d);
  else if (!strcmp(fn, "menu_open")) { if (!begruessung_sperrt(d)) open_menu->execute(); }
  else if (!strcmp(fn, "menu_top")) { if (!begruessung_sperrt(d)) open_menu_top->execute(); }
  else if (!strcmp(fn, "home")) go_home->execute();
  else if (!strcmp(fn, "display_off")) {
    // War es beim (ersten) Druck dunkel, hat der Druck es schon geweckt - dann nur an lassen.
    if (!begruessung_sperrt(d) && !d) { sofort_aus->value() = true; backlight_idle_timeout->execute(); }
  }
  else if (!strcmp(fn, "display_wake")) wake_backlight->execute();
  else if (!strcmp(fn, "voice_ptt")) { if (c.geste == tasten_konfig::LANG) voice_start->execute(); }
  else if (!strcmp(fn, "assistant_toggle")) toggle_assistant->execute();
  else if (!strcmp(fn, "activity")) aktivitaet_setzen(arg);
  else if (!strcmp(fn, "activity_next")) aktivitaet_setzen((activity->value() + 1) % aktivitaeten_anzahl());
  else if (!strcmp(fn, "activity_off")) aktivitaet_aus();
  else if (!strcmp(fn, "keyboard")) kbd_popup_zeigen->execute();
  else if (!strcmp(fn, "kbd_light")) toggle_kbd_light->execute();
  else if (!strcmp(fn, "mic_toggle")) toggle_mic->execute();
  else if (!strcmp(fn, "clean")) clean_run->execute();
  else if (!strcmp(fn, "power_off")) herunterfahren->execute();
  else if (!strcmp(fn, "touchpad")) touchpad::oeffnen();
  else if (!strcmp(fn, "maus_tastatur"))   // Touchpad <-> Tastatur (gleiche Popup-Gruppe), sonst das Touchpad
    eingabewahl::wechseln(cur_page->value() == 51 ? 1 : 0);
  else if (!strcmp(fn, "dock_anzeige")) { popup_seite->value() = -1; seite_zeigen(page_dock, LV_SCR_LOAD_ANIM_FADE_IN); }
  else if (!strcmp(fn, "musik_anzeige")) musik::zeigen(false);
  else if (!strcmp(fn, "keymode")) { if (cur_page->value() == 98) mn->tastenmodus(-1); }
  else if (!strcmp(fn, "popup_close")) { if (popup_seite->value() >= 0 && popup_seite->value() == cur_page->value()) popup_schliessen->execute(); }
  else ESP_LOGW("tasten", "Unbekannte Funktion '%s'", fn);
}


// Eingebaute Seiten, die im konfigurierbaren Menue als "Spezialseite" eingehaengt werden koennen.
// Name (wie im Panel) -> LVGL-Seite und cur_page-Nummer.
struct Spezial { const char *name; lvgl::LvPageType *seite; int nr; };
inline const std::vector<Spezial> &spezial_tabelle() {
  static const std::vector<Spezial> T = {
    {"media", page_media, 6}, {"ble", page_ble, 10}, {"voice_set", page_voice_set, 13}, {"service", page_service, 14},
    {"bright", page_bright, 22}, {"show", page_show, 23}, {"power", page_power, 27}, {"cam_view", page_cam_view, 37},
    {"cam_live", page_cam_live, 38}, {"keyboard", page_keyboard, 41}, {"ziffern", page_keyboard, 41},
    {"tastatur", page_keyboard, 41}, {"sd", page_sd, 42}, {"akku", page_akku, 43},
    {"dock", page_dock, 50}, {"touchpad", page_maus, 51}, {"musik", page_musik, 52}, {"extras", page_extras, 44}, {"gesten", page_gesten, 45},
  };
  return T;
}
// Name der eingebauten Seite mit dieser cur_page-Nummer (nullptr = keine Spezialseite)
inline const char *spezial_name(int nr) {
  for (const auto &s : spezial_tabelle()) if (s.nr == nr) return s.name;
  return nullptr;
}
inline void seite_zeigen(lvgl::LvPageType *seite, lv_screen_load_anim_t anim) {
  seite->get_parent()->show_page(seite->index, anim, 150);
}
// Menueseite zeigen (page_konfig) mit Aufbau-Animation: Kopfzeile, dann die Karten
inline void menue_zeigen() {
  seite_zeigen(page_konfig, LV_SCR_LOAD_ANIM_OVER_TOP);
  kopf_anim(lv_obj_get_parent(konf_back), konf_back);
  mn->einblenden(600);
}
inline int spezial_zeigen(const std::string &n) {
  for (const auto &s : spezial_tabelle()) {
    if (n != s.name) continue;
    if (s.nr == 6) media_from->value() = 3;   // Media-Seite aus dem Menue: zaehlt als Menueseite
    if (s.nr == 41) { kbd_popup->value() = false; if (n == "ziffern") kbd_mode->value() = 0; if (n == "tastatur") kbd_mode->value() = 1; }
    popup_seite->value() = -1;
    seite_zeigen(s.seite, LV_SCR_LOAD_ANIM_MOVE_LEFT);
    return s.nr;
  }
  ESP_LOGW("menue", "Unbekannte Spezialseite '%s'", n.c_str());
  return -1;
}

// Fortschritt der Konfigurations-Uebertragung auf dem Display (2026-10-03): Karte ueber jeder Seite
// (lv_layer_top), wie beim Firmware-Update. phase 1 beginnt, 2 laeuft, 3 uebernommen, 4 Fehler.
struct KonfigKarte { lv_obj_t *karte{nullptr}, *balken{nullptr}, *zeile{nullptr}; uint32_t gen{0}; };
inline KonfigKarte &konfig_karte() { static KonfigKarte k; return k; }
// Folgemodus (cfg.modi): kleine Leiste oben mit Namen und ablaufender Zeit; ms = 0 blendet sie aus.
// Bei dunklem Display nichts zeigen (der Modus selbst gilt trotzdem).
inline void modus_anzeige(const char *name, uint32_t ms) {
  static lv_obj_t *pille = nullptr, *text = nullptr, *balken = nullptr;
  if (ms == 0 || disp_off->value()) {
    if (pille != nullptr) lv_obj_add_flag(pille, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (pille == nullptr) {
    pille = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(pille);
    lv_obj_set_size(pille, 200, 34);
    lv_obj_align(pille, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(pille, lv_color_hex(0x1A1F27), 0);
    lv_obj_set_style_bg_opa(pille, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pille, 17, 0);
    lv_obj_set_style_border_width(pille, 2, 0);
    lv_obj_set_style_border_color(pille, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_clip_corner(pille, true, 0);
    lv_obj_clear_flag(pille, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(pille, LV_OBJ_FLAG_CLICKABLE);
    text = lv_label_create(pille);
    lv_obj_set_style_text_font(text, a15->get_lv_font(), 0);
    lv_obj_set_style_text_color(text, lv_color_hex(0xF1F3F5), 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_obj_set_width(text, 180);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(text, LV_ALIGN_CENTER, 0, -2);
    balken = lv_bar_create(pille);
    lv_obj_set_size(balken, 200, 3);
    lv_obj_align(balken, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(balken, 0, 1000);
    lv_obj_set_style_bg_opa(balken, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(balken, lv_color_hex(0x3B82F6), LV_PART_INDICATOR);
    lv_obj_set_style_radius(balken, 0, LV_PART_INDICATOR);
  }
  lv_label_set_text(text, name);
  lv_obj_clear_flag(pille, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(pille);
  // Restzeit als schrumpfender Balken
  lv_anim_del(balken, nullptr);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, balken);
  lv_anim_set_values(&a, 1000, 0);
  lv_anim_set_time(&a, ms);
  lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_bar_set_value((lv_obj_t *) o, v, LV_ANIM_OFF); });
  lv_anim_start(&a);
}

inline void konfig_anzeige(int phase, int pct, const char *text) {
  KonfigKarte &K = konfig_karte();
  lv_obj_t *&karte = K.karte, *&balken = K.balken, *&zeile = K.zeile;
  if (karte == nullptr) {
    karte = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(karte);
    lv_obj_set_size(karte, 210, 104);
    lv_obj_align(karte, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(karte, lv_color_hex(0x1A1F27), 0);
    lv_obj_set_style_bg_opa(karte, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(karte, 14, 0);
    lv_obj_set_style_border_width(karte, 2, 0);
    lv_obj_clear_flag(karte, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(karte, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(karte, [](lv_event_t *e) { lv_obj_add_flag((lv_obj_t *) lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *t = lv_label_create(karte);
    lv_obj_set_style_text_font(t, a15->get_lv_font(), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xF1F3F5), 0);
    lv_label_set_text(t, "Konfiguration");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
    balken = lv_bar_create(karte);
    lv_obj_set_size(balken, 180, 10);
    lv_bar_set_range(balken, 0, 100);
    lv_obj_align(balken, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(balken, lv_color_hex(0x4B5563), LV_PART_MAIN);
    lv_obj_set_style_radius(balken, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(balken, 5, LV_PART_INDICATOR);
    zeile = lv_label_create(karte);
    lv_obj_set_style_text_font(zeile, a13->get_lv_font(), 0);
    lv_obj_set_style_text_color(zeile, lv_color_hex(0x8B94A3), 0);
    lv_label_set_long_mode(zeile, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(zeile, 190);
    lv_obj_set_style_text_align(zeile, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(zeile, LV_ALIGN_TOP_MID, 0, 62);
  }
  const uint32_t gen = ++K.gen;   // jede neue Meldung macht ein laufendes Ausblenden ungueltig
  const uint32_t farbe = phase == 3 ? 0x22C55E : phase == 4 ? 0xEF4444 : 0x3B82F6;
  lv_obj_set_style_border_color(karte, lv_color_hex(farbe), 0);
  lv_obj_set_style_bg_color(balken, lv_color_hex(farbe), LV_PART_INDICATOR);
  char b[48];
  if (phase == 2) {
    if (pct >= 0) { lv_bar_set_value(balken, pct, LV_ANIM_ON); snprintf(b, sizeof(b), "Wird übertragen … %d %%", pct); }
    else snprintf(b, sizeof(b), "Wird übertragen …");
    lv_label_set_text(zeile, b);
  } else {
    lv_bar_set_value(balken, phase == 1 ? 0 : (phase == 3 ? 100 : lv_bar_get_value(balken)), LV_ANIM_ON);
    lv_label_set_text(zeile, text);
  }
  lv_obj_set_style_text_color(zeile, lv_color_hex(phase >= 3 ? farbe : 0x8B94A3), 0);
  lv_obj_remove_flag(karte, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(karte);
  // Ergebnis kurz stehen lassen, dann ausblenden (Fehler laenger)
  if (phase >= 3) {
    lv_timer_t *t = lv_timer_create([](lv_timer_t *t) {
      KonfigKarte &K = konfig_karte();
      if ((uint32_t) (uintptr_t) lv_timer_get_user_data(t) == K.gen) lv_obj_add_flag(K.karte, LV_OBJ_FLAG_HIDDEN);
      lv_timer_delete(t);
    }, phase == 3 ? 2500 : 6000, (void *) (uintptr_t) gen);
    (void) t;
  }
}

// Startseite: Bereiche aus dem Konfigurator ins Raster 6 x 8 (40-px-Zellen, Displaykoordinaten) legen.
// Ohne eigenen Aufbau bleibt alles exakt wie in apply_home_layout gesetzt.
inline void startseite_anwenden() {
  auto *t = tk_cfg;
  // Erst die eingebaute Anordnung (Werte wie im YAML von page_home) - falls vorher eine eigene galt
  lv_obj_align(menu_home_btn, LV_ALIGN_BOTTOM_MID, -54, -46); lv_obj_set_size(menu_home_btn, 104, 44);
  lv_obj_align(app_home_btn, LV_ALIGN_BOTTOM_MID, 54, -46); lv_obj_set_size(app_home_btn, 104, 44);
  lv_obj_align(home_dock, LV_ALIGN_BOTTOM_MID, 0, 12); lv_obj_set_size(home_dock, 240, 50);
  for (lv_obj_t *o : {home_status_bar, menu_home_btn, app_home_btn, home_dock, home_player_btn}) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!t->home_eigen()) return;
  lv_obj_t *seite = lv_obj_get_parent(home_dock);
  const int pl = lv_obj_get_style_pad_left(seite, LV_PART_MAIN), pt = lv_obj_get_style_pad_top(seite, LV_PART_MAIN);
  // Bereich in Pixel (Seitenkoordinaten), 2 px Rand je Zelle
  struct R { int x, y, w, h; };
  auto px = [&](int x, int y, int w, int h) { return R{x * 40 + 2 - pl, y * 40 + 2 - pt, w * 40 - 4, h * 40 - 4}; };
  // Objekt mittig in einen Bereich legen, hoechstens mw x mh gross (das Cover hat eine feste Groesse)
  auto mittig = [](lv_obj_t *o, R r, int mw, int mh) {
    const int w = std::min(r.w, mw), h = std::min(r.h, mh);
    lv_obj_set_align(o, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(o, r.x + (r.w - w) / 2, r.y + (r.h - h) / 2);
    lv_obj_set_size(o, w, h);
  };
  auto zeig = [](lv_obj_t *o, bool an) { if (an) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); };
  // Liegt ein Bereich genau auf seiner Standardstelle, bleibt die eingebaute (pixelgenaue) Anordnung
  auto std_stelle = [](int x, int y, int w, int h, int sx, int sy, int sw, int sh) { return x == sx && y == sy && w == sw && h == sh; };
  int x, y, w, h; bool an;
  if (t->home_bereich("status", x, y, w, h, an)) zeig(home_status_bar, an);
  if (t->home_bereich("menu", x, y, w, h, an)) {
    zeig(menu_home_btn, an);
    if (an && !std_stelle(x, y, w, h, 0, 6, 3, 1)) mittig(menu_home_btn, px(x, y, w, h), 240, 320);
  }
  if (t->home_bereich("apps", x, y, w, h, an)) {
    zeig(app_home_btn, an);
    if (an && !std_stelle(x, y, w, h, 3, 6, 3, 1)) mittig(app_home_btn, px(x, y, w, h), 240, 320);
  }
  if (t->home_bereich("dock", x, y, w, h, an)) {
    zeig(home_dock, an);
    if (an && !std_stelle(x, y, w, h, 0, 7, 6, 1)) {
      lv_obj_set_align(home_dock, LV_ALIGN_TOP_LEFT);
      lv_obj_set_pos(home_dock, x * 40 - pl, y * 40 + 2 - pt);
      lv_obj_set_size(home_dock, w * 40, h * 40 - 2);
    }
  }
  if (t->home_bereich("medien", x, y, w, h, an)) {
    zeig(home_player_btn, an);
    if (an && !std_stelle(x, y, w, h, 0, 1, 6, 5)) mittig(home_player_btn, px(x, y, w, h), 216, 172);   // Cover fest 216 x 172
  }
}

inline void einrichten() {
  auto *t = tk_cfg;
  t->intern = [](const char *fn, int arg, int geste, bool dunkel) {
    if (!strcmp(fn, "voice_ptt_ende")) {
      // Push-to-Talk losgelassen: voice_want SOFORT loeschen (voice_stop hat im Google-Zweig 1500 ms delay)
      voice_want->value() = false;
      voice_stop->execute();
      return;
    }
    intern(fn, arg, Ctx{0, (tasten_konfig::Geste) geste, dunkel, activity->value()});
  };
  mn->spezial = [](const std::string &n) { return spezial_zeigen(n); };
  mn->nach_hause = []() { go_home->execute(); };
  mn->zurueck_taste = []() { nav_back->execute(); };
  mn->einstellung = [](const std::string &k) { return einstellungen::finden(k); };
  mn->einst_auffrischen = [](uint32_t m) { einstellungen::auffrischen(m); };
  mn->bild = [](const std::string &q) -> const void * {
    if (q == "klingel_haus") return db_haus_image->get_lv_image_dsc();
    if (q == "klingel_wohnung") return db_wohn_image->get_lv_image_dsc();
    return nullptr;
  };
  mn->popup_auf = [](const std::string &s, bool wecken) { menue_popup(s, wecken); };
  mn->popup_zu = [](const std::string &s) {
    if (mn->ist_popup() && mn->popup_seite() == s) menue_popup_zu();
    else if (popup_min->value() == 98 && mn->popup_seite() == s) popup_min->value() = -1;
  };
  // Popup mit eingebautem Inhalt (2026-10-13): Tastatur. Ausloeser-Wert "zahlen" = Ziffernblock, sonst Buchstaben.
  mn->inhalt_auf = [](const std::string &inhalt, const std::string &wert, bool wecken) {
    if (inhalt != "tastatur") return;
    kbd_streamer_offen->value() = true;
    kbd_popup_mode->value() = wert == "zahlen" ? 0 : 1;
    if (wecken) wake_backlight->execute();
    // Per Menue-Taste weggelegt: nicht ungefragt wieder aufdraengen (die Menue-Taste holt es)
    if (cur_page->value() != 41 && !kbd_popup_zu->value()) kbd_popup_zeigen->execute();
  };
  mn->inhalt_zu = [](const std::string &inhalt) {
    if (inhalt != "tastatur") return;
    kbd_streamer_offen->value() = false;
    kbd_popup_zu->value() = false;
    if (cur_page->value() == 41 && kbd_popup->value()) kbd_popup_weg->execute();   // selbst geoeffnete Tastatur bleibt
  };
  t->anzeige = [](int phase, int pct, const char *text) { konfig_anzeige(phase, pct, text); };
  t->modus_anzeige = [](const char *name, uint32_t ms) { modus_anzeige(name, ms); };
  // Neue Konfiguration: Aktivitaetsleiste neu aufbauen (Namen/Anzahl koennen sich geaendert haben)
  t->bei_neuer_konfig([]() {
    // Gleiches Geraet (Platz) wie vorher aktiv lassen, auch wenn die Aktivitaeten umsortiert wurden
    if (dock().slot >= 0)
      for (int i = 0; i < aktivitaeten_anzahl(); i++)
        if (aktivitaet_slot(i) == dock().slot) { activity->value() = i; break; }
    if (activity->value() >= aktivitaeten_anzahl()) activity->value() = 0;
    apply_activity->execute();
    apply_home_layout->execute();
  });
  t->darf_laden = []() {
    if (voice_active->value()) return false;
    wartung_bis->value() = millis() + 30000;  // Schlaf/WLAN-Aus waehrend des Ladens sperren
    return true;
  };
}

// Aus on_key. true = von der Belegung verarbeitet; false = Navigation auf den Seiten der Remote.
inline bool ereignis(int keycode, bool pressed) {
  auto *t = tk_cfg;
  // Der Tasten-Test behaelt seine eigene Logik; auf Menueseiten gehoeren
  // D-Pad/OK/Zurueck der Navigation.
  const int p = cur_page->value();
  // Menueseite: Halte-Knoepfe per OK und der Tastenmodus (D-Pad steuert z. B. die Kamera) zuerst
  if (p == 98 && !key_was_dark->value() && mn->taste(keycode, pressed)) { if (pressed) held_key->value() = 0; return true; }
  // Display dunkel (und bleibt es): Menue nicht steuern, sondern wie auf der Startseite das Geraet der Aktivitaet
  // (2026-10-07) - sobald das Display an ist, gehoert das Steuerkreuz wieder dem Menue.
  const bool dunkel = key_was_dark->value() && disp_off->value();
  // Touchpad: Steuerkreuz/OK/Zurueck/Kanal steuern die Maus (Einstellung "Steuerkreuz als Maus")
  if (p == 51 && !dunkel && !button_test_mode->value() && touchpad::taste(keycode, pressed)) { if (pressed) held_key->value() = 0; return true; }
  // Tasten eines laufenden Folgemodus gehoeren ihm auch auf den Menueseiten
  const bool darf = t->aktiv() && !button_test_mode->value() &&
                    (t->modus_taste(keycode) || !(p != 0 && p != 50 && p != 51 && !dunkel && nav_taste(keycode)));
  // Dock-Anzeige (50) und Touchpad (51): Tasten wie auf der Startseite (2026-10-17)
  const bool r = t->ereignis(keycode, pressed, activity->value(), key_was_dark->value(), darf);
  if (r && pressed) held_key->value() = 0;
  return r;
}

// Weck-Regel: die Taste mit Menue-/Display-Funktion weckt; ohne Belegung die Menuetaste (31).
inline bool weckt(int keycode) {
  auto *t = tk_cfg;
  if (!t->aktiv()) return keycode == 31;
  return t->weckt(keycode, activity->value());
}

}  // namespace tasten
