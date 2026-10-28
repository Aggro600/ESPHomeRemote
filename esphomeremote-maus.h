#pragma once
// Touchpad (2026-10-17): das Display als Maus fuer PC, Tablet oder Streamer (Seite page_maus, cur_page 51).
// Nur fuer Bluetooth-Plaetze, bei denen im Konfigurator "Maus/Touchpad" an ist - die anderen (z. B. Google TV)
// sehen weiterhin kein Zeigegeraet (sonst baut Android laufende Apps beim Verbinden neu auf).
//   Wischen = Zeiger bewegen wie am Laptop: der Finger wird live verfolgt, Verstaerkung nach Fingertempo
//   (langsam = genau, schnell = weit; Grundtempo maus_tempo %), gesendet gesammelt alle 15 ms. Tippen = Linksklick,
//   Doppeltippen + Halten = Ziehen, lange druecken = Rechtsklick, Streifen rechts = Scrollrad.
//   Oben: X, Geraetename, Umschalter Maus | Tastatur | Ziffern. Unten: Links/Rechts-Taste, Makros & Zwischenablage.
//   Steuerkreuz (maus_dpad, 2026-10-17): Pfeile = Zeiger (halten = schneller, Tempo maus_dpad_tempo %), OK = Klick,
//   OK lang = Rechtsklick, OK halten + Pfeile = Ziehen, Kanal +/- = Scrollen, Zurueck je nach maus_zurueck.
//   Zurueck lang (0,6 s) = Knopf-Modus: Pfeile waehlen X / Umschalter / Klicktasten / Makros, OK drueckt,
//   Zurueck (kurz oder lang) wieder Maus.
//   Touchpad, Tastatur und Ziffern sind EIN Popup (eingabewahl): Menuetaste legt es weg und holt es zurueck,
//   X schliesst die ganze Gruppe; Umschalter oben rechts oder Funktion "maus_tastatur" auf einer Taste.
// Text tippen braucht die volle Tastatur fuer den Platz; ohne sie tippt HA per ADB am Streamer (wie die Tastatur-Seite).

// ---------------------------------------------------------------- Eingabe-Umschalter (2026-10-18)
// Touchpad (Seite 51), Tastatur und Ziffernblock (Seite 41) sind EIN Popup mit drei Ansichten: gleiche Kopfzeile
// wie das Menue (blaues X-Feld links, tasten::kopf_linie) und rechts der Umschalter Maus | Tastatur | Ziffern.
// Umschalten behaelt die Herkunft der Gruppe - einmal X/Zurueck fuehrt dorthin zurueck (vorher musste man jede
// Ansicht einzeln schliessen, weil sich das Touchpad die Tastatur als Herkunft gemerkt hatte).
namespace gestenseite { inline bool namen_eingabe(); }   // esphomeremote-gestenseite.h (kommt danach)

namespace eingabewahl {

enum { MAUS = 0, TASTATUR = 1, ZIFFERN = 2 };
static const uint32_t ICON[3] = {0xF037D, 0xF097B, 0xF03A0};   // mouse, keyboard-outline, numeric

struct Wahl { lv_obj_t *box{nullptr}; lv_obj_t *kn[3]{}; };
inline Wahl &kbd() { static Wahl w; return w; }    // Kopfzeile der Tastatur-Seite
inline Wahl &maus() { static Wahl w; return w; }   // Kopfzeile des Touchpads

inline void wechseln(int ziel);

inline std::string utf8(uint32_t cp) {
  std::string s;
  s += (char) (0xF0 | (cp >> 18)); s += (char) (0x80 | ((cp >> 12) & 0x3F));
  s += (char) (0x80 | ((cp >> 6) & 0x3F)); s += (char) (0x80 | (cp & 0x3F));
  return s;
}

// Drei Knoepfe nebeneinander am Ende einer Kopfzeile (Flex-Reihe)
inline void aufbauen(lv_obj_t *kopf, Wahl &w) {
  if (w.box != nullptr) return;
  w.box = lv_obj_create(kopf);
  lv_obj_remove_style_all(w.box);
  lv_obj_set_size(w.box, LV_SIZE_CONTENT, 30);
  lv_obj_set_flex_flow(w.box, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(w.box, 3, 0);
  lv_obj_remove_flag(w.box, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = lv_button_create(w.box);
    lv_obj_set_size(b, 32, 30);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xF1F3F5), LV_STATE_FOCUSED);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, mdi_klein->get_lv_font(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xF1F3F5), 0);
    lv_label_set_text(l, utf8(ICON[i]).c_str());
    lv_obj_center(l);
    lv_obj_add_event_cb(b, [](lv_event_t *e) {
      last_input_ms->value() = millis();
      wechseln((int) (intptr_t) lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, (void *) (intptr_t) i);
    w.kn[i] = b;
  }
}

// Aktive Ansicht blau, die anderen dunkel (Rahmen in Feldfarbe, Fokus = weisser Rahmen)
inline void markieren(Wahl &w, int aktiv) {
  for (int i = 0; i < 3; i++) {
    if (w.kn[i] == nullptr) continue;
    const uint32_t f = i == aktiv ? 0x3B82F6 : 0x232A35;
    lv_obj_set_style_bg_color(w.kn[i], lv_color_hex(f), 0);
    lv_obj_set_style_border_color(w.kn[i], lv_color_hex(f), 0);
    lv_obj_remove_state(w.kn[i], LV_STATE_FOCUSED);
  }
}

// Tastatur-Seite: Umschalter in die YAML-Kopfzeile (Eltern von back_keyboard). Beim Namen-Eintippen der
// Gesten-Seite ausgeblendet - dort soll man nicht umschalten.
inline void kbd_einrichten() {
  Wahl &w = kbd();
  aufbauen(lv_obj_get_parent(back_keyboard), w);
  markieren(w, kbd_mode->value() == 0 ? ZIFFERN : TASTATUR);
  if (gestenseite::namen_eingabe()) lv_obj_add_flag(w.box, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(w.box, LV_OBJ_FLAG_HIDDEN);
}
inline bool kbd_umschalter_da() { return kbd().box != nullptr && !lv_obj_has_flag(kbd().box, LV_OBJ_FLAG_HIDDEN); }

// Ansicht wechseln: 0 Maus, 1 Tastatur, 2 Ziffern. Von ausserhalb der Gruppe: oeffnet als Popup.
inline void wechseln(int ziel) {
  const int p = cur_page->value();
  const bool gruppe = p == 41 || p == 51;
  // Herkunft der Gruppe: offenes Popup -> seine Herkunft; Tastatur als Menue-Seite -> das Menue; sonst diese Seite
  int von = p;
  if (gruppe) von = (popup_seite->value() == p) ? popup_von->value() : 98;
  if (ziel == MAUS) {
    if (p == 51) return;
    kbd_popup->value() = false;
    if (gruppe) { popup_von->value() = von; popup_seite->value() = 51; popup_min->value() = -1; }
    else popup_oeffnet(51);   // von aussen wie jedes Popup (aus dem Menue: Stand fuer die Rueckkehr merken)
    wake_backlight->execute();
    tasten::seite_zeigen(page_maus, gruppe ? LV_SCR_LOAD_ANIM_FADE_IN : LV_SCR_LOAD_ANIM_MOVE_LEFT);
    return;
  }
  const int modus = ziel == ZIFFERN ? 0 : 1;
  if (p == 41) {   // schon auf der Tastatur-Seite: nur die Belegung tauschen
    if (kbd_mode->value() == modus) return;
    kbd_mode->value() = modus;
    kbd_popup_mode->value() = modus;
    kbd_shift->value() = false;
    kbd_sel->value() = -1;
    kbd_setup->execute();
    lv_label_set_text(kbd_title, modus == 0 ? "Ziffern" : "Tastatur");
    markieren(kbd(), ziel);
    return;
  }
  kbd_popup_mode->value() = modus;
  kbd_popup_zu->value() = false;
  kbd_popup_zeigen->execute();   // popup_oeffnet(41) - die Herkunft gleich danach auf die der Gruppe setzen
  popup_von->value() = von;
  popup_seite->value() = 41;
}

}  // namespace eingabewahl

namespace touchpad {

struct Zustand {
  bool gebaut{false};
  lv_obj_t *titel{nullptr}, *pad{nullptr}, *streifen{nullptr}, *hinweis{nullptr}, *makro_box{nullptr};
  uint32_t druck_ms{0}, letzter_tipp_ms{0};
  int32_t weg{0};
  bool ziehen{false}, lang{false}, links_gehalten{false};
  float rx{0}, ry{0}, rad{0};
  uint8_t knoepfe{0};
  // Steuerkreuz
  lv_timer_t *timer{nullptr};
  uint8_t pfeile{0};            // Bits: 1 hoch, 2 runter, 4 links, 8 rechts
  uint32_t pfeil_ms{0};
  float px{0}, py{0};
  bool ok_unten{false}, ok_zieht{false}, ok_fertig{false};
  uint32_t ok_ms{0};
  int scroll{0};                // +1 / -1 solange Kanal +/- gehalten
  uint32_t scroll_ms{0};
  // Finger: Bewegung sammeln, im Takt senden; Tempo-Messung
  uint32_t zug_ms{0};
  float tempo_glatt{0};
  bool pad_gedrueckt{false};
  // Scroll-Streifen mit Schwung
  float rad_v{0};               // Raststufen je Takt nach dem Loslassen
  bool streifen_gedrueckt{false};
  uint32_t streifen_ms{0};
  // Knopf-Modus (Steuerkreuz waehlt die Knoepfe statt den Zeiger zu bewegen)
  bool knopfmodus{false};
  int kfi{0};
  std::vector<lv_obj_t *> kn;   // Kopf: 0 X, 1 Maus, 2 Tastatur, 3 Ziffern - unten: 4 Links, 5 Rechts, 6 Makros
  bool zurueck_unten{false};
  uint32_t zurueck_ms{0};
};
inline Zustand &z() { static Zustand s; return s; }

inline std::string utf8(uint32_t cp) { return dockanz::utf8(cp); }

// Touchpad ueber das Dock (2026-10-22): ist der aktive Platz am Dock gekoppelt und die Remote selbst nicht verbunden
// (oder "Dock zuerst"), gehen Bewegungen per Funk ans Dock (tasten_konfig::maus_ueber_dock)
inline void maus_raus(uint8_t b, int8_t dx, int8_t dy, int8_t rad) {
  if (!tk_cfg->maus_ueber_dock(b, dx, dy, rad)) kb->maus(b, dx, dy, rad);
}
inline void senden(int8_t dx, int8_t dy, int8_t rad) { maus_raus(z().knoepfe, dx, dy, rad); }

inline void klick(uint8_t b) {
  maus_raus(b, 0, 0, 0);
  maus_raus(z().knoepfe, 0, 0, 0);
}

inline void schnell() { kb->request_conn_profile(false); }

// Tastatur/Text ueber das Dock (2026-10-23): wie das Touchpad - ist das Geraet am Dock gekoppelt und die Remote selbst
// nicht verbunden (oder "Dock zuerst"), geht es per Funk ans Dock; sonst/als Ausweg sendet die Remote selbst.
inline void taste_raus(uint8_t mod, uint8_t code) {
  if (!tk_cfg->eingabe_ueber_dock(1, code, mod, [mod, code]() { kb->send_key_combo(mod, code); })) kb->send_key_combo(mod, code);
}
inline void consumer_raus(uint16_t code) {
  if (!tk_cfg->eingabe_ueber_dock(0, code, 0, [code]() { kb->send_consumer(code); })) kb->send_consumer(code);
}

inline void tippen(const std::string &t) {
  // Ueber das Dock: dessen Platz meldet die volle Tastatur selbst (Dockingstation > Bluetooth-Geraete: "Tastatur")
  if (tk_cfg->text_ueber_dock(t, [t]() { kb->send_string(t); })) return;
  bool nur_ziffern = true;
  for (char ch : t) if (ch < '0' || ch > '9') nur_ziffern = false;
  if (!kb->tastatur_voll() && !nur_ziffern) {
    std::string q = "input text '";
    for (char ch : t) {
      if (ch == ' ') q += "%s";
      else if (ch == '\'') q += "'\\''";
      else q += ch;
    }
    q += "'";
    adb_tippen->execute(q);
  } else {
    kb->send_string(t);
  }
}

inline void makros_zeigen(bool an);
inline void timer_an();

// Als Popup oeffnen (aus der Tastatur heraus: gleiche Gruppe, Herkunft bleibt - s. eingabewahl)
inline void oeffnen() { eingabewahl::wechseln(eingabewahl::MAUS); }
inline void schliessen() {
  if (popup_seite->value() == 51 && cur_page->value() == 51) popup_schliessen->execute();
  else go_home->execute();
}

inline void makro(int i) {
  makros_zeigen(false);
  schnell();
  // feste Zwischenablage-Funktionen (Strg + Taste)
  static const uint8_t FEST[] = {0x04, 0x1B, 0x06, 0x19, 0x1D};   // A, X, C, V, Z
  if (i < 0) { taste_raus(0x01, FEST[-i - 1]); return; }
  JsonArrayConst m = tk_cfg->konfig("makros");
  if (i >= (int) m.size()) return;
  JsonObjectConst e = m[i];
  if (e["k"].is<JsonArrayConst>()) taste_raus((uint8_t) (e["k"][0] | 0), (uint8_t) (e["k"][1] | 0));
  else tippen(e["t"] | "");
}

inline lv_obj_t *knopf(lv_obj_t *p, int x, int y, int w, int h, const char *txt, const lv_font_t *f, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x232A35), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x3B82F6), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 2, LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(b, lv_color_hex(0xF1F3F5), LV_STATE_FOCUSED);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xF1F3F5), 0);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_ALL, ud);
  return b;
}

inline void makros_zeigen(bool an) {
  auto &s = z();
  if (s.makro_box) { lv_obj_delete(s.makro_box); s.makro_box = nullptr; }
  if (!an) return;
  s.makro_box = lv_obj_create(maus_root);
  lv_obj_set_pos(s.makro_box, 6, 46);
  lv_obj_set_size(s.makro_box, 228, 216);
  lv_obj_set_style_bg_color(s.makro_box, lv_color_hex(0x0E1116), 0);
  lv_obj_set_style_border_color(s.makro_box, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_border_width(s.makro_box, 2, 0);
  lv_obj_set_style_radius(s.makro_box, 12, 0);
  lv_obj_set_style_pad_all(s.makro_box, 6, 0);
  lv_obj_set_flex_flow(s.makro_box, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(s.makro_box, 6, 0);
  lv_obj_set_style_pad_column(s.makro_box, 6, 0);
  auto add = [&](const char *t, int idx) {
    lv_obj_t *b = knopf(s.makro_box, 0, 0, 104, 36, t, a13->get_lv_font(), [](lv_event_t *e) {
      if (lv_event_get_code(e) == LV_EVENT_CLICKED) makro((int) (intptr_t) lv_event_get_user_data(e));
    }, (void *) (intptr_t) idx);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_obj_set_width(l, 96);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  };
  static const char *FEST[] = {"Alles", "Ausschn.", "Kopieren", "Einfügen", "Rückgängig"};
  for (int i = 0; i < 5; i++) add(FEST[i], -i - 1);
  JsonArrayConst m = tk_cfg->konfig("makros");
  int i = 0;
  for (JsonObjectConst e : m) add(e["n"] | "?", i++);
  if (!kb->tastatur_voll()) {
    lv_obj_t *l = lv_label_create(s.makro_box);
    lv_obj_set_width(l, 212);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, a13->get_lv_font(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x8B94A3), 0);
    lv_label_set_text(l, "Kürzel brauchen beim Bluetooth-Platz die Eingabe \"Tastatur\" (Einstellungen > Bluetooth oder Konfigurator).");
  }
}

inline void pad_ereignis(lv_event_t *e) {
  auto &s = z();
  const lv_event_code_t c = lv_event_get_code(e);
  lv_indev_t *in = lv_indev_active();
  const uint32_t jetzt = millis();
  if (c == LV_EVENT_PRESSED) {
    schnell();
    last_input_ms->value() = jetzt;
    s.druck_ms = jetzt;
    s.zug_ms = jetzt;
    s.tempo_glatt = 0;
    s.pad_gedrueckt = true;
    timer_an();
    s.weg = 0;
    s.lang = false;
    s.rx = s.ry = 0;
    // Doppeltippen + Halten = Ziehen (linke Taste bleibt gedrueckt)
    if (jetzt - s.letzter_tipp_ms < 300) { s.ziehen = true; s.knoepfe |= 0x01; senden(0, 0, 0); }
  } else if (c == LV_EVENT_PRESSING) {
    lv_point_t v;
    lv_indev_get_vect(in, &v);
    if (v.x == 0 && v.y == 0) return;
    s.weg += abs(v.x) + abs(v.y);
    last_input_ms->value() = jetzt;
    // Fingertempo (px/ms, geglaettet) -> Verstaerkung: langsam ~2x (genau), schnell bis ~11x (quer ueber den Bildschirm)
    const float d = sqrtf((float) (v.x * v.x + v.y * v.y));
    const float dt = std::max<uint32_t>(jetzt - s.zug_ms, 5);
    s.zug_ms = jetzt;
    s.tempo_glatt += (d / dt - s.tempo_glatt) * 0.5f;
    const float f = maus_tempo->value() / 100.0f * (2.0f + 6.0f * std::min(s.tempo_glatt, 1.5f));
    s.rx += v.x * f;   // gesendet wird im Takt (dpad_takt), gesammelt - so geht nichts verloren
    s.ry += v.y * f;
  } else if (c == LV_EVENT_LONG_PRESSED) {
    if (s.weg < 10 && !s.ziehen) { s.lang = true; klick(0x02); }
  } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
    s.pad_gedrueckt = false;
    if (s.ziehen) { s.ziehen = false; s.knoepfe &= ~0x01; senden(0, 0, 0); s.letzter_tipp_ms = 0; return; }
    if (!s.lang && s.weg < 10 && jetzt - s.druck_ms < 250) { klick(0x01); s.letzter_tipp_ms = jetzt; }
  }
}

inline void streifen_ereignis(lv_event_t *e) {
  auto &s = z();
  const lv_event_code_t c = lv_event_get_code(e);
  const uint32_t jetzt = millis();
  if (c == LV_EVENT_PRESSED) { schnell(); s.rad = 0; s.rad_v = 0; s.streifen_gedrueckt = true; s.streifen_ms = jetzt; timer_an(); last_input_ms->value() = jetzt; return; }
  if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) { s.streifen_gedrueckt = false; return; }   // Schwung laeuft im Takt aus
  if (c != LV_EVENT_PRESSING) return;
  lv_point_t v;
  lv_indev_get_vect(lv_indev_active(), &v);
  if (v.y == 0) return;
  const float dt = std::max<uint32_t>(jetzt - s.streifen_ms, 5);
  s.streifen_ms = jetzt;
  // wie am Handy: Finger nach oben = Inhalt nach oben (Rad nach unten); schneller Zug = mehr Stufen
  const float stufen = -v.y / 10.0f * (1.0f + 2.0f * std::min(fabsf(v.y) / dt, 2.0f));
  s.rad += stufen;
  s.rad_v += (stufen / dt * 15.0f - s.rad_v) * 0.5f;   // Stufen je 15-ms-Takt fuer den Schwung
  last_input_ms->value() = jetzt;
}

inline void hinweis_setzen() {
  auto &s = z();
  if (!s.gebaut) return;
  const int slot = kb->active_host_slot();
  std::string name = tk_cfg->slot_name(slot);
  if (name.empty()) name = "Platz " + std::to_string(slot + 1);
  lv_label_set_text(s.titel, name.c_str());
  std::string dock;
  uint8_t dp = 0;
  // Ueber das Dock (2026-10-23): das Geraet muss nur am Dock gekoppelt sein (dort mit Maus) - kein Hinweis
  if (kb->maus_aktiv() || tk_cfg->eingabe_dock_ziel(dock, dp, true)) lv_obj_add_flag(s.hinweis, LV_OBJ_FLAG_HIDDEN);
  else {
    lv_label_set_text(s.hinweis, kb->is_connected()
        ? ("Für \"" + name + "\" ist die Maus aus. Einstellungen > Bluetooth > Eingabe beim Platz (oder Konfigurator > Bluetooth): Maus wählen, dann neu koppeln.").c_str()
        : "Kein Bluetooth-Gerät verbunden.");
    lv_obj_clear_flag(s.hinweis, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------- Steuerkreuz
inline void links(bool an) {
  auto &s = z();
  if (an) s.knoepfe |= 0x01; else s.knoepfe &= ~0x01;
  senden(0, 0, 0);
}

inline void dpad_takt(lv_timer_t *) {
  auto &s = z();
  const uint32_t jetzt = millis();
  // Finger auf dem Pad + Streifen: Gesammeltes in einem Bericht senden (Bluetooth verliert sonst Pakete)
  int8_t rad = 0;
  if (!s.streifen_gedrueckt && fabsf(s.rad_v) > 0.05f) { s.rad += s.rad_v; s.rad_v *= 0.93f; }   // Schwung laeuft aus
  else if (!s.streifen_gedrueckt) s.rad_v = 0;
  if (fabsf(s.rad) >= 1) { rad = (int8_t) std::max(-15, std::min(15, (int) s.rad)); s.rad -= rad; }
  if (cur_page->value() == 51 && (fabsf(s.rx) >= 1 || fabsf(s.ry) >= 1 || rad)) {
    const int dx = std::max(-127, std::min(127, (int) s.rx)), dy = std::max(-127, std::min(127, (int) s.ry));
    s.rx -= dx; s.ry -= dy;
    senden(dx, dy, rad);
  }
  if (cur_page->value() != 51) { s.pfeile = 0; s.scroll = 0; if (s.ok_zieht) links(false); s.ok_unten = s.ok_zieht = false; }
  // OK lang ohne Bewegung = Rechtsklick
  if (s.ok_unten && !s.ok_zieht && !s.ok_fertig && jetzt - s.ok_ms > 600) { s.ok_fertig = true; klick(0x02); }
  if (s.pfeile) {
    const float t = std::min<uint32_t>(jetzt - s.pfeil_ms, 1500) / 1500.0f;
    const float v = maus_dpad_tempo->value() / 100.0f * (1.5f + t * t * 10.0f);   // je 15-ms-Takt: sanft anfahren, dann schnell
    const int ax = ((s.pfeile & 8) ? 1 : 0) - ((s.pfeile & 4) ? 1 : 0);
    const int ay = ((s.pfeile & 2) ? 1 : 0) - ((s.pfeile & 1) ? 1 : 0);
    s.px += ax * v; s.py += ay * v;
    const int dx = std::max(-127, std::min(127, (int) s.px)), dy = std::max(-127, std::min(127, (int) s.py));
    s.px -= dx; s.py -= dy;
    if ((dx || dy) && s.ok_unten && !s.ok_zieht && !s.ok_fertig) { s.ok_zieht = true; links(true); }   // OK + Pfeile = Ziehen
    if (dx || dy) senden(dx, dy, 0);
    last_input_ms->value() = jetzt;
  }
  if (s.scroll && jetzt - s.scroll_ms >= 120) { s.scroll_ms = jetzt; senden(0, 0, s.scroll); last_input_ms->value() = jetzt; }
  if (!s.pfeile && !s.scroll && !s.ok_unten && !s.pad_gedrueckt && !s.streifen_gedrueckt && fabsf(s.rad_v) <= 0.05f &&
      fabsf(s.rx) < 1 && fabsf(s.ry) < 1)
    lv_timer_pause(s.timer);
}

inline void timer_an() {
  auto &s = z();
  if (!s.timer) s.timer = lv_timer_create(dpad_takt, 15, nullptr);
  lv_timer_resume(s.timer);
}

inline void hinweis_setzen();
inline void knopf_fokus() {
  auto &s = z();
  for (size_t i = 0; i < s.kn.size(); i++) {
    if ((int) i == s.kfi && s.knopfmodus) lv_obj_add_state(s.kn[i], LV_STATE_FOCUSED);
    else lv_obj_remove_state(s.kn[i], LV_STATE_FOCUSED);
  }
  if (s.knopfmodus) lv_label_set_text(s.titel, "Knöpfe");
  else hinweis_setzen();
}
inline void knopfmodus(bool an) {
  auto &s = z();
  s.knopfmodus = an;
  s.kfi = an ? 2 : 0;   // Start auf "Tastatur" - der haeufigste Wechsel
  knopf_fokus();
}

// Tasten im Touchpad (aus tasten::ereignis). true = verbraucht.
inline bool taste(int k, bool gedrueckt) {
  auto &s = z();
  if (!maus_dpad->value()) return false;
  const uint32_t jetzt = millis();
  // Zurueck: kurz = Einstellung "Zurueck-Taste" (im Knopf-Modus: zurueck zur Maus), lang = Knopf-Modus an/aus
  if (k == 13) {
    if (gedrueckt) { if (!s.zurueck_unten) { s.zurueck_unten = true; s.zurueck_ms = jetzt; } return true; }
    if (!s.zurueck_unten) return true;
    s.zurueck_unten = false;
    if (jetzt - s.zurueck_ms >= 600 || s.knopfmodus) { knopfmodus(!s.knopfmodus && jetzt - s.zurueck_ms >= 600); return true; }
    const int m = maus_zurueck->value();
    if (m == 1) consumer_raus(0x0224);   // Android "Zurueck"
    else if (m == 2) klick(0x02);
    else { makros_zeigen(false); schliessen(); }
    return true;
  }
  if (s.knopfmodus) {
    if (!gedrueckt) return k == 14 || k == 35 || k == 15 || k == 32 || k == 34;
    const int n = s.kn.size();
    const bool oben = s.kfi <= 3;   // Kopf: X, Maus, Tastatur, Ziffern - unten: Links, Rechts, Makros
    switch (k) {
      case 15: s.kfi = oben ? (s.kfi + 3) % 4 : 4 + (s.kfi - 4 + 2) % 3; break;
      case 32: s.kfi = oben ? (s.kfi + 1) % 4 : 4 + (s.kfi - 4 + 1) % 3; break;
      case 14: if (!oben) s.kfi = 2; break;
      case 35: if (oben) s.kfi = 4; break;
      case 34: {
        if (s.kfi < 0 || s.kfi >= n) return true;
        lv_obj_t *b = s.kn[s.kfi];
        const bool ende = s.kfi == 0 || s.kfi == 2 || s.kfi == 3;   // X / Tastatur / Ziffern verlassen die Seite
        if (ende) s.knopfmodus = false;
        lv_obj_send_event(b, LV_EVENT_PRESSED, nullptr);
        lv_obj_send_event(b, LV_EVENT_RELEASED, nullptr);
        lv_obj_send_event(b, LV_EVENT_CLICKED, nullptr);
        if (ende) return true;
        break;
      }
      default: return false;
    }
    knopf_fokus();
    return true;
  }
  uint8_t bit = k == 14 ? 1 : k == 35 ? 2 : k == 15 ? 4 : k == 32 ? 8 : 0;
  if (bit) {
    if (gedrueckt) {
      if (!(s.pfeile & bit)) { if (!s.pfeile) { s.pfeil_ms = jetzt; s.px = s.py = 0; } s.pfeile |= bit; schnell(); timer_an(); }
    } else s.pfeile &= ~bit;
    return true;
  }
  if (k == 34) {
    if (gedrueckt) {
      if (!s.ok_unten) { s.ok_unten = true; s.ok_zieht = s.ok_fertig = false; s.ok_ms = jetzt; schnell(); timer_an(); }
    } else if (s.ok_unten) {
      s.ok_unten = false;
      if (s.ok_zieht) links(false);
      else if (!s.ok_fertig) klick(0x01);
      s.ok_zieht = false;
    }
    return true;
  }
  if (k == 42 || k == 22) {   // Kanal +/- = Scrollrad
    if (gedrueckt) { if (!s.scroll) { s.scroll = k == 42 ? 1 : -1; s.scroll_ms = jetzt; senden(0, 0, s.scroll); schnell(); timer_an(); } }
    else s.scroll = 0;
    return true;
  }
  return false;
}

inline void aufbauen() {
  auto &s = z();
  if (s.gebaut) return;
  s.gebaut = true;
  lv_obj_t *r = maus_root;
  lv_obj_clean(r);
  s.kn.clear();
  // Kopfzeile wie im Menue (2026-10-18): X im blauen Feld, Titel, rechts der Umschalter Maus | Tastatur | Ziffern
  lv_obj_t *hd = lv_obj_create(r);
  lv_obj_remove_style_all(hd);
  lv_obj_set_pos(hd, 0, 0);
  lv_obj_set_size(hd, 240, 40);
  lv_obj_set_style_bg_color(hd, lv_color_hex(0x0E1116), 0);
  lv_obj_set_style_bg_opa(hd, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left(hd, 6, 0);
  lv_obj_set_style_pad_right(hd, 6, 0);
  lv_obj_set_style_pad_top(hd, 6, 0);
  lv_obj_set_style_pad_bottom(hd, 4, 0);
  lv_obj_set_flex_flow(hd, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hd, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(hd, 8, 0);
  lv_obj_remove_flag(hd, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *x = lv_button_create(hd);
  lv_obj_set_size(x, 46, 30);
  lv_obj_set_style_bg_color(x, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_radius(x, 10, 0);
  lv_obj_set_style_shadow_width(x, 0, 0);
  lv_obj_set_style_border_width(x, 2, 0);
  lv_obj_set_style_border_color(x, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_border_color(x, lv_color_hex(0xF1F3F5), LV_STATE_FOCUSED);
  lv_obj_t *xl = lv_label_create(x);
  lv_obj_set_style_text_font(xl, a15->get_lv_font(), 0);
  lv_obj_set_style_text_color(xl, lv_color_hex(0xF1F3F5), 0);
  lv_label_set_text(xl, "X");
  lv_obj_center(xl);
  lv_obj_add_event_cb(x, [](lv_event_t *e) { makros_zeigen(false); schliessen(); }, LV_EVENT_CLICKED, nullptr);
  s.titel = lv_label_create(hd);
  lv_obj_set_style_text_font(s.titel, a15->get_lv_font(), 0);
  lv_obj_set_style_text_color(s.titel, lv_color_hex(0xF1F3F5), 0);
  lv_obj_set_height(s.titel, 22);
  lv_obj_set_width(s.titel, 10);
  lv_obj_set_flex_grow(s.titel, 1);
  lv_label_set_long_mode(s.titel, LV_LABEL_LONG_DOT);
  eingabewahl::aufbauen(hd, eingabewahl::maus());
  s.kn.push_back(x);
  for (lv_obj_t *b : eingabewahl::maus().kn) s.kn.push_back(b);
  tasten::kopf_linie(hd, x);
  // Flaeche
  s.pad = lv_obj_create(r);
  lv_obj_set_pos(s.pad, 6, 46);
  lv_obj_set_size(s.pad, 196, 216);
  lv_obj_set_style_bg_color(s.pad, lv_color_hex(0x1A1F27), 0);
  lv_obj_set_style_border_color(s.pad, lv_color_hex(0x2A3240), 0);
  lv_obj_set_style_border_width(s.pad, 1, 0);
  lv_obj_set_style_radius(s.pad, 12, 0);
  lv_obj_clear_flag(s.pad, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
  lv_obj_add_flag(s.pad, LV_OBJ_FLAG_PRESS_LOCK);   // Finger darf ueber den Rand hinaus - der Zeiger folgt weiter
  lv_obj_add_event_cb(s.pad, pad_ereignis, LV_EVENT_ALL, nullptr);
  lv_obj_t *ic = lv_label_create(s.pad);
  lv_obj_set_style_text_font(ic, mdi_klein->get_lv_font(), 0);
  lv_obj_set_style_text_color(ic, lv_color_hex(0x2A3240), 0);
  lv_label_set_text(ic, utf8(0xF01BE).c_str());
  lv_obj_center(ic);
  s.hinweis = lv_label_create(s.pad);
  lv_obj_set_width(s.hinweis, 176);
  lv_label_set_long_mode(s.hinweis, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(s.hinweis, a13->get_lv_font(), 0);
  lv_obj_set_style_text_color(s.hinweis, lv_color_hex(0xF59E0B), 0);
  lv_obj_align(s.hinweis, LV_ALIGN_TOP_MID, 0, 0);
  // Scroll-Streifen
  s.streifen = lv_obj_create(r);
  lv_obj_set_pos(s.streifen, 206, 46);
  lv_obj_set_size(s.streifen, 28, 216);
  lv_obj_set_style_bg_color(s.streifen, lv_color_hex(0x232A35), 0);
  lv_obj_set_style_border_width(s.streifen, 0, 0);
  lv_obj_set_style_radius(s.streifen, 10, 0);
  lv_obj_clear_flag(s.streifen, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
  lv_obj_add_flag(s.streifen, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(s.streifen, streifen_ereignis, LV_EVENT_ALL, nullptr);
  lv_obj_t *si = lv_label_create(s.streifen);
  lv_obj_set_style_text_font(si, mdi_klein->get_lv_font(), 0);
  lv_obj_set_style_text_color(si, lv_color_hex(0x8B94A3), 0);
  lv_label_set_text(si, utf8(0xF0E79).c_str());
  lv_obj_center(si);
  // Untere Reihe: Links, Rechts, Tastatur, Makros
  auto taste = [](lv_event_t *e) {
    const int b = (int) (intptr_t) lv_event_get_user_data(e);
    const lv_event_code_t c = lv_event_get_code(e);
    auto &s = z();
    if (c == LV_EVENT_PRESSED) { schnell(); s.knoepfe |= b; senden(0, 0, 0); }
    else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) { s.knoepfe &= ~b; senden(0, 0, 0); }
  };
  // Untere Reihe: Links-, Rechtsklick, Makros (Tastatur/Ziffern stehen jetzt oben im Umschalter)
  s.kn.push_back(knopf(r, 6, 268, 73, 46, utf8(0xF1D08).c_str(), mdi_klein->get_lv_font(), taste, (void *) (intptr_t) 0x01));
  s.kn.push_back(knopf(r, 83, 268, 74, 46, utf8(0xF1D0B).c_str(), mdi_klein->get_lv_font(), taste, (void *) (intptr_t) 0x02));
  s.kn.push_back(knopf(r, 161, 268, 73, 46, utf8(0xF0279).c_str(), mdi_klein->get_lv_font(), [](lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) makros_zeigen(z().makro_box == nullptr);
  }, nullptr));
}

// on_load der Seite
inline void geladen() {
  aufbauen();
  eingabewahl::markieren(eingabewahl::maus(), eingabewahl::MAUS);
  z().knopfmodus = false;
  z().zurueck_unten = false;
  knopf_fokus();
  makros_zeigen(false);
  hinweis_setzen();
  schnell();
}

}  // namespace touchpad
