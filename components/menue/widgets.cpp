// Widgets fuer Menue- und Startseiten (2026-10-27, Nutzerwunsch wie im OpenRemote-Original): Uhr, Wetter, Akku,
// Medien, Timer, Kamera und Kombi (bis zu drei Teile nebeneinander). Gross ab zwei Rasterzeilen, sonst schmal.
// Daten: Zustaende per HTTP wie alle Kacheln (StatesView liefert Wetter/Timer mit Zusatzfeldern), Bilder ueber den
// Endpunkt /api/esphomeremote_konfig/bild (Kamera-Schnappschuss, Cover) in zwei Bildplaetzen der YAML (wbild_0/1).
#include "menue.h"
#include "esphome/components/tasten_konfig/tasten_konfig.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cmath>

namespace esphome {
namespace menue {

static const char *const TAG = "menue.widget";
static const int ZEILE = 40, LUECKE = 4;
static const uint32_t C_TEXT = 0xF1F3F5, C_GRAU = 0x8B94A3, C_KARTE = 0x1A1F27, C_BLAU = 0x3B82F6, C_GRUEN = 0x22C55E;
static std::string g_wkey[2];   // was gerade im Bildplatz liegt (Entitaet + Titel bzw. Kamera)

static lv_obj_t *wtext_(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t farbe) {
  lv_obj_t *l = lv_label_create(p);
  if (f) lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_label_set_text(l, t);
  return l;
}
// Einzeilig mit "..." - kuerzt nur bei fester Hoehe, sonst bricht LVGL um und der Text laeuft in die Zeilen darunter
static void punkte_(lv_obj_t *l) {
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)));
}
static lv_obj_t *wbox_(lv_obj_t *p) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}
static lv_obj_t *wbalken_(lv_obj_t *p, int breite, uint32_t farbe) {
  lv_obj_t *b = lv_bar_create(p);
  lv_obj_set_size(b, breite, 4);
  lv_bar_set_range(b, 0, 1000);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x4B5563), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(farbe), LV_PART_INDICATOR);
  lv_obj_set_style_radius(b, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(b, 2, LV_PART_INDICATOR);
  return b;
}
static std::string utf8_(uint32_t cp) {
  std::string s;
  s += (char) (0xF0 | (cp >> 18));
  s += (char) (0x80 | ((cp >> 12) & 0x3F));
  s += (char) (0x80 | ((cp >> 6) & 0x3F));
  s += (char) (0x80 | (cp & 0x3F));
  return s;
}

// HA-Wetterzustand -> Symbol (mdi) und deutscher Text
struct WetterInfo { const char *zustand; uint32_t cp; const char *text; };
static const WetterInfo WETTER[] = {
    {"sunny", 0xF0599, "Sonnig"}, {"clear-night", 0xF0594, "Klar"}, {"partlycloudy", 0xF0595, "Teils bewölkt"},
    {"cloudy", 0xF0590, "Bewölkt"}, {"fog", 0xF0591, "Nebel"}, {"hail", 0xF0592, "Hagel"},
    {"lightning", 0xF0593, "Gewitter"}, {"lightning-rainy", 0xF067E, "Gewitter, Regen"}, {"pouring", 0xF0596, "Starkregen"},
    {"rainy", 0xF0597, "Regen"}, {"snowy", 0xF0598, "Schnee"}, {"snowy-rainy", 0xF067F, "Schneeregen"},
    {"windy", 0xF059D, "Windig"}, {"windy-variant", 0xF059E, "Windig, bewölkt"}, {"exceptional", 0xF0F37, "Unwetter"}};
static const WetterInfo &wetter_(const char *z) {
  for (const auto &w : WETTER) if (!strcmp(w.zustand, z)) return w;
  static const WetterInfo unbekannt{"", 0xF0590, "–"};
  return unbekannt;
}

uint8_t Menue::widget_maske_(const Item &it) const {
  if (it.wart != W_KOMBI) return 1 << it.wart;
  uint8_t m = 0;
  for (JsonVariantConst v : it.j["teile"].as<JsonArrayConst>()) {
    const char *t = v | "";
    if (!strcmp(t, "uhr")) m |= 1 << W_UHR;
    else if (!strcmp(t, "wetter")) m |= 1 << W_WETTER;
    else if (!strcmp(t, "akku")) m |= 1 << W_AKKU;
    else if (!strcmp(t, "medien")) m |= 1 << W_MEDIEN;
  }
  if (m == 0) m = (1 << W_UHR) | (1 << W_WETTER);
  if (m & (1 << W_MEDIEN)) {
    const char *mz = it.j["mz"] | "immer";
    const bool laeuft = medien_laeuft_(it.j["me"] | "");
    if (!strcmp(mz, "nur") && !laeuft && m != (1 << W_MEDIEN)) m &= ~(1 << W_MEDIEN);
    else if (!strcmp(mz, "ueber") && laeuft) m = 1 << W_MEDIEN;
    else if (!strcmp(mz, "ueber") && !laeuft && m != (1 << W_MEDIEN)) m &= ~(1 << W_MEDIEN);
  }
  return m;
}

bool Menue::medien_laeuft_(const char *e) const {
  if (e == nullptr || !*e) return false;
  auto f = zustand_cache_.find(PStr(e));
  return f != zustand_cache_.end() && (f->second.state == "playing" || f->second.state == "paused" || f->second.state == "buffering");
}

bool Menue::widget_weg_(JsonObjectConst j) const {
  if (strcmp(j["art"] | "", "medien") != 0 || strcmp(j["leer"] | "", "weg") != 0) return false;
  return !medien_laeuft_(j["entity"] | "");
}

void Menue::widget_karte_(Item &it) {
  static const struct { const char *n; WidgetArt a; } ARTEN[] = {{"uhr", W_UHR}, {"wetter", W_WETTER}, {"akku", W_AKKU},
      {"medien", W_MEDIEN}, {"timer", W_TIMER}, {"kamera", W_KAMERA}, {"kombi", W_KOMBI}};
  const char *art = it.j["art"] | "uhr";
  it.wart = W_UHR;
  for (auto &a : ARTEN) if (!strcmp(a.n, art)) it.wart = a.a;
  lv_obj_t *k = lv_obj_create(root_);
  lv_obj_remove_style_all(k);
  lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(k, lv_color_hex(C_KARTE), 0);
  lv_obj_set_style_radius(k, 10, 0);
  lv_obj_set_style_border_width(k, 2, 0);
  lv_obj_set_style_border_color(k, lv_color_hex(C_KARTE), 0);
  lv_obj_set_style_clip_corner(k, true, 0);
  lv_obj_set_pos(k, 6 + (int) (it.x * zelle_), 2 + it.y * ZEILE);
  lv_obj_set_size(k, (int) (it.w * zelle_) - LUECKE, it.h * ZEILE - LUECKE);
  lv_obj_clear_flag(k, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(k, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  it.obj = k;
  it.bg = C_KARTE;
  // Bildplatz: Kamera immer, Medien nur gross und breit genug fuer das Cover
  const bool bild = it.wart == W_KAMERA || (it.h >= 2 && it.w >= 5 && (it.wart == W_MEDIEN || it.wart == W_KOMBI));
  if (bild && wslots_frei_ < 2 && wbild_laden) {
    it.wslot = wslots_frei_++;
    g_wkey[it.wslot].clear();
  }
  widget_teile_(it);
}

void Menue::widget_teile_(Item &it) {
  lv_obj_clean(it.obj);
  for (auto &o : it.wo) o = nullptr;
  const uint8_t m = widget_maske_(it);
  it.wbau = m;
  const bool gross = it.h >= 2;
  const int W = (int) (it.w * zelle_) - LUECKE - 4, H = it.h * ZEILE - LUECKE - 4;
  // Reihenfolge: wie in "teile", sonst die eine Art
  uint8_t folge[4];
  int n = 0;
  if (it.wart == W_KOMBI) {
    for (JsonVariantConst v : it.j["teile"].as<JsonArrayConst>()) {
      const char *t = v | "";
      const uint8_t a = !strcmp(t, "uhr") ? W_UHR : !strcmp(t, "wetter") ? W_WETTER : !strcmp(t, "akku") ? W_AKKU
                        : !strcmp(t, "medien") ? W_MEDIEN : W_KEIN;
      if (a != W_KEIN && (m & (1 << a)) && n < 3) folge[n++] = a;
    }
    if (n == 0) for (uint8_t a : {W_UHR, W_WETTER}) if (m & (1 << a)) folge[n++] = a;
  } else {
    folge[n++] = it.wart;
  }
  const int spalte = n > 0 ? (W - (n - 1) * 4) / n : W;
  for (int i = 0; i < n; i++) {
    lv_obj_t *c = wbox_(it.obj);
    lv_obj_set_pos(c, 2 + i * (spalte + 4), 2);
    lv_obj_set_size(c, spalte, H);
    if (i > 0) {   // feine Trennlinie zwischen den Teilen
      lv_obj_t *t = wbox_(it.obj);
      lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(t, lv_color_hex(0x2E3643), 0);
      lv_obj_set_size(t, 1, H - 12);
      lv_obj_set_pos(t, 2 + i * (spalte + 4) - 2, 8);
    }
    widget_teil_(it, c, folge[i], spalte, H, gross);
  }
}

void Menue::widget_teil_(Item &it, lv_obj_t *p, uint8_t art, int breite, int hoehe, bool gross) {
  const lv_font_t *f_gr = f_gross_ ? f_gross_ : f_text_, *f_xl = f_xl_ ? f_xl_ : f_gr, *f_xs = f_xs_ ? f_xs_ : f_klein_;
  switch (art) {
    case W_UHR: {
      if (gross) {
        const bool riesig = f_uhr_ != nullptr && breite >= 130 && hoehe >= 70;
        it.wo[WR_ZEIT] = wtext_(p, "--:--", riesig ? f_uhr_ : f_xl, C_TEXT);
        lv_obj_align(it.wo[WR_ZEIT], LV_ALIGN_CENTER, 0, strcmp(it.j["datum"] | "nach", "aus") ? -10 : 0);
        if (strcmp(it.j["datum"] | "nach", "aus")) {
          it.wo[WR_DATUM] = wtext_(p, "", f_klein_, C_GRAU);
          lv_obj_set_width(it.wo[WR_DATUM], breite);
          lv_obj_set_style_text_align(it.wo[WR_DATUM], LV_TEXT_ALIGN_CENTER, 0);
          punkte_(it.wo[WR_DATUM]);
          lv_obj_align_to(it.wo[WR_DATUM], it.wo[WR_ZEIT], LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
          if (!strcmp(it.j["datum"] | "nach", "vor")) {   // Datum ueber der Uhrzeit
            lv_obj_align(it.wo[WR_ZEIT], LV_ALIGN_CENTER, 0, 8);
            lv_obj_align_to(it.wo[WR_DATUM], it.wo[WR_ZEIT], LV_ALIGN_OUT_TOP_MID, 0, 0);
          }
        }
      } else {
        it.wo[WR_ZEIT] = wtext_(p, "--:--", f_gr, C_TEXT);
        const bool vor = !strcmp(it.j["datum"] | "nach", "vor");
        lv_obj_align(it.wo[WR_ZEIT], vor ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, vor ? -4 : 4, 0);
        if (strcmp(it.j["datum"] | "nach", "aus") && breite >= 110) {
          it.wo[WR_DATUM] = wtext_(p, "", f_klein_, C_GRAU);
          lv_obj_align(it.wo[WR_DATUM], vor ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, vor ? 4 : -4, 0);
        }
      }
      break;
    }
    case W_WETTER: {
      if (gross && f_wetter_ != nullptr) {
        it.wo[WR_W_ICON] = wtext_(p, utf8_(0xF0590).c_str(), f_wetter_, C_TEXT);
        it.wo[WR_W_TEMP] = wtext_(p, "–", f_xl, C_TEXT);
        it.wo[WR_W_TEXT] = wtext_(p, "", f_klein_, C_GRAU);
        punkte_(it.wo[WR_W_TEXT]);
        lv_obj_set_width(it.wo[WR_W_TEXT], breite - 4);
        const bool fc = breite >= 150 && hoehe >= 96;   // Vorhersage unten, wenn Platz ist
        lv_obj_align(it.wo[WR_W_ICON], LV_ALIGN_TOP_LEFT, 2, fc ? 0 : hoehe / 2 - 34);
        lv_obj_align_to(it.wo[WR_W_TEMP], it.wo[WR_W_ICON], LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        lv_obj_align_to(it.wo[WR_W_TEXT], it.wo[WR_W_ICON], LV_ALIGN_OUT_BOTTOM_LEFT, 2, -2);
        if (fc) {
          lv_obj_t *r = wbox_(p);
          lv_obj_set_size(r, breite, 34);
          lv_obj_align(r, LV_ALIGN_BOTTOM_LEFT, 0, 0);
          lv_obj_set_layout(r, LV_LAYOUT_FLEX);
          lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
          lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
          it.wo[WR_W_FC] = r;
        }
      } else {
        it.wo[WR_W_ICON] = wtext_(p, "", f_icon_ ? f_icon_ : f_text_, C_TEXT);
        lv_obj_align(it.wo[WR_W_ICON], LV_ALIGN_LEFT_MID, 2, 0);
        it.wo[WR_W_TEMP] = wtext_(p, "–", f_text_, C_TEXT);
        lv_obj_align(it.wo[WR_W_TEMP], LV_ALIGN_LEFT_MID, 28, 0);
        if (breite >= 120) {
          it.wo[WR_W_TEXT] = wtext_(p, "", f_klein_, C_GRAU);
          punkte_(it.wo[WR_W_TEXT]);
          lv_obj_set_width(it.wo[WR_W_TEXT], breite - 92);
          lv_obj_align(it.wo[WR_W_TEXT], LV_ALIGN_RIGHT_MID, -4, 0);
          lv_obj_set_style_text_align(it.wo[WR_W_TEXT], LV_TEXT_ALIGN_RIGHT, 0);
        }
      }
      break;
    }
    case W_AKKU: {
      const char *gl = icon_("battery");
      it.wo[WR_A_ICON] = wtext_(p, gl ? gl : "", f_icon_ ? f_icon_ : f_text_, C_TEXT);
      if (gross) {
        it.wo[WR_A_WERT] = wtext_(p, "–", f_xl, C_TEXT);
        lv_obj_align(it.wo[WR_A_WERT], LV_ALIGN_CENTER, 0, -8);
        lv_obj_align_to(it.wo[WR_A_ICON], it.wo[WR_A_WERT], LV_ALIGN_OUT_TOP_MID, 0, -2);
        it.wo[WR_A_BAR] = wbalken_(p, std::min(breite - 16, 120), C_GRUEN);
        lv_obj_set_height(it.wo[WR_A_BAR], 6);
        lv_obj_align(it.wo[WR_A_BAR], LV_ALIGN_BOTTOM_MID, 0, -8);
      } else {
        lv_obj_align(it.wo[WR_A_ICON], LV_ALIGN_LEFT_MID, 2, 0);
        it.wo[WR_A_WERT] = wtext_(p, "–", f_text_, C_TEXT);
        lv_obj_align(it.wo[WR_A_WERT], LV_ALIGN_LEFT_MID, 28, 0);
      }
      break;
    }
    case W_MEDIEN: {
      const int idx = (int) (&it - items_.data());
      // Knopf (prev/play/next) mit eigenem Klick; user_data = Index * 4 + Funktion
      auto knopf = [&](lv_obj_t *par, const char *icon, int fn, int groesse) {
        lv_obj_t *b = lv_obj_create(par);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, groesse, groesse);
        lv_obj_set_style_radius(b, groesse / 2, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(fn == 1 ? C_BLAU : 0x232A35), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x4B5563), LV_STATE_PRESSED);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        const char *gl = icon_(icon);
        lv_obj_t *l = wtext_(b, gl ? gl : "", f_icon_ ? f_icon_ : f_text_, C_TEXT);
        lv_obj_center(l);
        lv_obj_set_user_data(b, (void *) (intptr_t) (idx * 4 + fn));
        lv_obj_add_event_cb(b, Menue::wknopf_cb_, LV_EVENT_CLICKED, this);
        return b;
      };
      int x0 = 0;
      if (gross && it.wslot >= 0 && breite >= 180) {   // Cover links, quadratisch
        const int s = hoehe - 4;
        lv_obj_t *rahmen = wbox_(p);
        lv_obj_set_size(rahmen, s, s);
        lv_obj_set_pos(rahmen, 0, 2);
        lv_obj_set_style_radius(rahmen, 8, 0);
        lv_obj_set_style_clip_corner(rahmen, true, 0);
        lv_obj_set_style_bg_opa(rahmen, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(rahmen, lv_color_hex(0x0E1116), 0);
        const char *gl = icon_("music-note");
        lv_obj_center(wtext_(rahmen, gl ? gl : "", f_icon_ ? f_icon_ : f_text_, C_GRAU));
        it.wo[WR_M_BILD] = lv_image_create(rahmen);
        lv_obj_center(it.wo[WR_M_BILD]);
        lv_obj_add_flag(it.wo[WR_M_BILD], LV_OBJ_FLAG_HIDDEN);
        x0 = s + 8;
      }
      const int bw = breite - x0 - (x0 > 0 ? 6 : 0);   // neben dem Cover rechts etwas Luft zum Kartenrand
      if (gross) {
        it.wo[WR_M_TITEL] = wtext_(p, "Nichts läuft", f_text_, C_TEXT);
        punkte_(it.wo[WR_M_TITEL]);
        lv_obj_set_width(it.wo[WR_M_TITEL], bw);
        lv_obj_set_pos(it.wo[WR_M_TITEL], x0, 2);
        it.wo[WR_M_SUB] = wtext_(p, "", f_klein_, C_GRAU);
        punkte_(it.wo[WR_M_SUB]);
        lv_obj_set_width(it.wo[WR_M_SUB], bw);
        lv_obj_set_pos(it.wo[WR_M_SUB], x0, 22);
        it.wo[WR_M_BAR] = wbalken_(p, bw, 0xEF4444);
        lv_obj_set_pos(it.wo[WR_M_BAR], x0, 42);
        lv_obj_add_flag(it.wo[WR_M_BAR], LV_OBJ_FLAG_HIDDEN);
        if (hoehe >= 70) {
          const int g = std::min(34, (bw - 8) / 3);
          lv_obj_t *r = wbox_(p);
          lv_obj_set_size(r, bw, g);
          lv_obj_set_pos(r, x0, hoehe - g - 2);
          lv_obj_set_layout(r, LV_LAYOUT_FLEX);
          lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
          lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
          knopf(r, "skip-previous", 0, g);
          it.wo[WR_M_PLAY] = lv_obj_get_child(knopf(r, "play-pause", 1, g), 0);
          knopf(r, "skip-next", 2, g);
        }
      } else {
        lv_obj_t *b = knopf(p, "play-pause", 1, std::min(30, hoehe - 2));
        lv_obj_align(b, LV_ALIGN_LEFT_MID, 0, 0);
        it.wo[WR_M_PLAY] = lv_obj_get_child(b, 0);
        it.wo[WR_M_TITEL] = wtext_(p, "Nichts läuft", f_klein_, C_TEXT);
        punkte_(it.wo[WR_M_TITEL]);
        lv_obj_set_width(it.wo[WR_M_TITEL], breite - 38);
        lv_obj_align(it.wo[WR_M_TITEL], LV_ALIGN_TOP_LEFT, 36, 0);
        it.wo[WR_M_SUB] = wtext_(p, "", f_xs, C_GRAU);
        punkte_(it.wo[WR_M_SUB]);
        lv_obj_set_width(it.wo[WR_M_SUB], breite - 38);
        lv_obj_align(it.wo[WR_M_SUB], LV_ALIGN_BOTTOM_LEFT, 36, 0);
      }
      break;
    }
    case W_TIMER: {
      if (gross) {
        it.wo[WR_T_NAME] = wtext_(p, it.j["label"] | "Timer", f_klein_, C_GRAU);
        punkte_(it.wo[WR_T_NAME]);
        lv_obj_set_width(it.wo[WR_T_NAME], breite - 4);
        lv_obj_align(it.wo[WR_T_NAME], LV_ALIGN_TOP_LEFT, 2, 0);
        it.wo[WR_T_ZEIT] = wtext_(p, "--:--", f_uhr_ != nullptr && breite >= 150 && hoehe >= 70 ? f_uhr_ : f_xl, C_TEXT);
        lv_obj_align(it.wo[WR_T_ZEIT], LV_ALIGN_CENTER, 0, 2);
        it.wo[WR_T_BAR] = wbalken_(p, breite - 8, C_BLAU);
        lv_obj_align(it.wo[WR_T_BAR], LV_ALIGN_BOTTOM_MID, 0, -4);
      } else {
        const char *gl = icon_("timer-outline");
        lv_obj_align(wtext_(p, gl ? gl : "", f_icon_ ? f_icon_ : f_text_, C_BLAU), LV_ALIGN_LEFT_MID, 2, 0);
        it.wo[WR_T_NAME] = wtext_(p, it.j["label"] | "Timer", f_klein_, C_GRAU);
        punkte_(it.wo[WR_T_NAME]);
        lv_obj_set_width(it.wo[WR_T_NAME], breite - 110);
        lv_obj_align(it.wo[WR_T_NAME], LV_ALIGN_LEFT_MID, 28, 0);
        it.wo[WR_T_ZEIT] = wtext_(p, "--:--", f_gr, C_TEXT);
        lv_obj_align(it.wo[WR_T_ZEIT], LV_ALIGN_RIGHT_MID, -4, 0);
      }
      break;
    }
    case W_KAMERA: {
      lv_obj_set_style_bg_color(it.obj, lv_color_hex(0x000000), 0);
      lv_obj_set_style_border_color(it.obj, lv_color_hex(0x000000), 0);
      it.bg = 0x000000;
      it.wo[WR_K_PH] = wtext_(p, it.wslot >= 0 ? "Bild wird geladen ..." : "Kein Bildplatz frei", f_klein_, C_GRAU);
      lv_obj_center(it.wo[WR_K_PH]);
      it.wo[WR_K_BILD] = lv_image_create(it.obj);   // ueber die ganze Karte (ohne Spalten-Innenrand)
      lv_obj_center(it.wo[WR_K_BILD]);
      lv_obj_add_flag(it.wo[WR_K_BILD], LV_OBJ_FLAG_HIDDEN);
      const char *n = it.j["label"] | "";
      if (*n) {
        lv_obj_t *l = wtext_(it.obj, n, f_xs, C_TEXT);
        lv_obj_set_style_bg_opa(l, LV_OPA_60, 0);
        lv_obj_set_style_bg_color(l, lv_color_hex(0x000000), 0);
        lv_obj_set_style_pad_hor(l, 4, 0);
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 4, -3);
      }
      break;
    }
    default:
      break;
  }
}

void Menue::widget_zustand_(Item &it) {
  if (it.obj == nullptr) return;
  // Kombi: Medien kommt/geht mit der Wiedergabe -> Inhalt neu aufbauen
  if (it.wart == W_KOMBI && widget_maske_(it) != it.wbau) widget_teile_(it);
  const char *e = it.j["entity"] | "";
  char b[64];
  // Wetter
  if (it.wo[WR_W_TEMP] != nullptr) {
    const char *we = it.wart == W_KOMBI ? (it.j["we"] | "") : e;
    auto f = zustand_cache_.find(PStr(we));
    if (f != zustand_cache_.end()) {
      const EntZustand &z = f->second;
      const WetterInfo &w = wetter_(z.state.c_str());
      const auto ta = z.attr.find(PStr("temperature"));
      const char *t = ta != z.attr.end() ? ta->second.c_str() : "";
      if (*t) {
        snprintf(b, sizeof(b), "%s°", t);
        for (char *c = b; *c; c++) if (*c == '.') *c = ',';
      } else {
        snprintf(b, sizeof(b), "–");
      }
      lv_label_set_text(it.wo[WR_W_TEMP], b);
      if (lv_obj_get_style_text_font(it.wo[WR_W_ICON], LV_PART_MAIN) == f_wetter_ && f_wetter_ != nullptr) {
        lv_label_set_text(it.wo[WR_W_ICON], utf8_(w.cp).c_str());
      } else {
        // schmal: Symbol aus der Menue-Schrift (Namen weather-*)
        static const struct { uint32_t cp; const char *n; } NAMEN[] = {
            {0xF0599, "weather-sunny"}, {0xF0594, "weather-night"}, {0xF0595, "weather-partly-cloudy"},
            {0xF0590, "weather-cloudy"}, {0xF0591, "weather-fog"}, {0xF0592, "weather-hail"},
            {0xF0593, "weather-lightning"}, {0xF067E, "weather-lightning-rainy"}, {0xF0596, "weather-pouring"},
            {0xF0597, "weather-rainy"}, {0xF0598, "weather-snowy"}, {0xF067F, "weather-snowy-rainy"},
            {0xF059D, "weather-windy"}, {0xF059E, "weather-windy-variant"}, {0xF0F37, "weather-sunny-alert"}};
        const char *gl = nullptr;
        for (auto &n : NAMEN) if (n.cp == w.cp) gl = icon_(n.n);
        lv_label_set_text(it.wo[WR_W_ICON], gl ? gl : "");
      }
      if (it.wo[WR_W_TEXT] != nullptr) {
        const auto fa = z.attr.find(PStr("humidity"));
        if (it.h >= 2 && fa != z.attr.end() && !fa->second.empty()) snprintf(b, sizeof(b), "%s · %s %%", w.text, fa->second.c_str());
        else snprintf(b, sizeof(b), "%s", w.text);
        lv_label_set_text(it.wo[WR_W_TEXT], b);
      }
      if (it.wo[WR_W_FC] != nullptr) {
        lv_obj_clean(it.wo[WR_W_FC]);
        const auto fc = z.attr.find(PStr("fc"));
        std::string rest = fc != z.attr.end() ? fc->second.c_str() : "";
        int n = 0;
        while (!rest.empty() && n < 4) {
          const size_t sem = rest.find(';');
          std::string tag = rest.substr(0, sem);
          rest = sem == std::string::npos ? "" : rest.substr(sem + 1);
          if (n++ == 0) continue;   // heute steht schon oben
          char tg[8] = "", zs[24] = "";
          int mx = -99, mn = -99;
          if (sscanf(tag.c_str(), "%7[^|]|%23[^|]|%d|%d", tg, zs, &mx, &mn) < 2) continue;
          lv_obj_t *s = wbox_(it.wo[WR_W_FC]);
          lv_obj_set_size(s, LV_SIZE_CONTENT, 34);
          lv_obj_set_layout(s, LV_LAYOUT_FLEX);
          lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
          lv_obj_set_flex_align(s, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
          lv_obj_set_style_pad_column(s, 2, 0);
          wtext_(s, tg, f_xs_ ? f_xs_ : f_klein_, C_GRAU);
          const WetterInfo &wi = wetter_(zs);
          static const struct { uint32_t cp; const char *n; } KN[] = {
              {0xF0599, "weather-sunny"}, {0xF0594, "weather-night"}, {0xF0595, "weather-partly-cloudy"},
              {0xF0590, "weather-cloudy"}, {0xF0591, "weather-fog"}, {0xF0592, "weather-hail"},
              {0xF0593, "weather-lightning"}, {0xF067E, "weather-lightning-rainy"}, {0xF0596, "weather-pouring"},
              {0xF0597, "weather-rainy"}, {0xF0598, "weather-snowy"}, {0xF067F, "weather-snowy-rainy"},
              {0xF059D, "weather-windy"}, {0xF059E, "weather-windy-variant"}, {0xF0F37, "weather-sunny-alert"}};
          const char *gl = nullptr;
          for (auto &k : KN) if (k.cp == wi.cp) gl = icon_(k.n);
          if (gl) wtext_(s, gl, f_icon_, C_TEXT);
          if (mx > -99) {
            snprintf(b, sizeof(b), mn > -99 ? "%d°/%d°" : "%d°", mx, mn);
            wtext_(s, b, f_xs_ ? f_xs_ : f_klein_, C_TEXT);
          }
        }
      }
    }
  }
  // Medien
  if (it.wo[WR_M_TITEL] != nullptr) {
    const char *me = it.wart == W_KOMBI ? (it.j["me"] | "") : e;
    auto f = zustand_cache_.find(PStr(me));
    const bool laeuft = medien_laeuft_(me);
    std::string titel, sub;
    if (f != zustand_cache_.end()) {
      const EntZustand &z = f->second;
      auto at = [&](const char *k) { auto x = z.attr.find(PStr(k)); return x != z.attr.end() ? std::string(x->second.c_str()) : std::string(); };
      titel = at("media_title");
      sub = at("media_artist");
      if (sub.empty()) sub = at("app_name");
      if (titel.empty() && laeuft) titel = at("app_name");
    }
    lv_label_set_text(it.wo[WR_M_TITEL], laeuft && !titel.empty() ? titel.c_str() : (laeuft ? "Wiedergabe" : "Nichts läuft"));
    if (it.wo[WR_M_SUB] != nullptr) lv_label_set_text(it.wo[WR_M_SUB], laeuft ? sub.c_str() : "");
    if (it.wo[WR_M_PLAY] != nullptr) {
      const bool spielt = f != zustand_cache_.end() && f->second.state == "playing";
      const char *gl = icon_(spielt ? "pause" : "play");
      if (gl) lv_label_set_text(it.wo[WR_M_PLAY], gl);
    }
    if (it.wo[WR_M_BILD] != nullptr && it.wslot >= 0) {
      const std::string key = laeuft ? std::string(me) + "|" + titel : std::string();
      if (key.empty()) { lv_obj_add_flag(it.wo[WR_M_BILD], LV_OBJ_FLAG_HIDDEN); g_wkey[it.wslot].clear(); }
      else if (key != g_wkey[it.wslot]) { g_wkey[it.wslot] = key; widget_bild_(it, true); }
    }
  }
  widget_tick_();
}

// Jede Sekunde: Uhr, Akku, Timer-Countdown, Medien-Fortschritt, Kamera nachladen
void Menue::widget_tick_() {
  int h = 0, mi = 0, wt = 0, tg = 0, mo = 0;
  const bool zeit_ok = zeit && zeit(h, mi, wt, tg, mo);
  int pct = -1;
  bool laedt = false;
  if (akku) akku(pct, laedt);
  static const char *const WTAG[] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
  static const char *const MON[] = {"Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
  char b[48];
  for (auto &it : items_) {
    if (it.typ != T_WIDGET || it.obj == nullptr) continue;
    if (it.wo[WR_ZEIT] != nullptr) {
      if (zeit_ok) snprintf(b, sizeof(b), "%02d:%02d", h, mi); else snprintf(b, sizeof(b), "--:--");
      if (strcmp(lv_label_get_text(it.wo[WR_ZEIT]), b)) lv_label_set_text(it.wo[WR_ZEIT], b);
      if (it.wo[WR_DATUM] != nullptr && zeit_ok && wt >= 0 && wt < 7 && mo >= 1 && mo <= 12) {
        snprintf(b, sizeof(b), "%s, %d. %s", WTAG[wt], tg, MON[mo - 1]);
        if (strcmp(lv_label_get_text(it.wo[WR_DATUM]), b)) lv_label_set_text(it.wo[WR_DATUM], b);
      }
    }
    if (it.wo[WR_A_WERT] != nullptr && pct >= 0) {
      snprintf(b, sizeof(b), "%d %%", pct);
      lv_label_set_text(it.wo[WR_A_WERT], b);
      const char *gl = icon_(laedt ? "battery-charging" : "battery");
      if (gl && it.wo[WR_A_ICON] != nullptr) {
        lv_label_set_text(it.wo[WR_A_ICON], gl);
        lv_obj_set_style_text_color(it.wo[WR_A_ICON], lv_color_hex(laedt ? C_GRUEN : pct <= 15 ? 0xEF4444 : C_TEXT), 0);
      }
      if (it.wo[WR_A_BAR] != nullptr) {
        lv_bar_set_value(it.wo[WR_A_BAR], pct * 10, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(it.wo[WR_A_BAR], lv_color_hex(pct <= 15 ? 0xEF4444 : pct <= 30 ? 0xF59E0B : C_GRUEN), LV_PART_INDICATOR);
      }
    }
    if (it.wo[WR_T_ZEIT] != nullptr) {
      auto f = zustand_cache_.find(PStr(it.j["entity"] | ""));
      if (f != zustand_cache_.end()) {
        const EntZustand &z = f->second;
        auto zahl = [&](const char *k) { auto x = z.attr.find(PStr(k)); return x != z.attr.end() ? atoi(x->second.c_str()) : 0; };
        int rest = zahl("rest");
        const int dauer = zahl("dauer");
        if (z.state == "active") rest = std::max(0, rest - (int) ((millis() - z.pos_ms) / 1000));
        if (rest >= 3600) snprintf(b, sizeof(b), "%d:%02d:%02d", rest / 3600, rest / 60 % 60, rest % 60);
        else snprintf(b, sizeof(b), "%d:%02d", rest / 60, rest % 60);
        lv_label_set_text(it.wo[WR_T_ZEIT], b);
        lv_obj_set_style_text_color(it.wo[WR_T_ZEIT], lv_color_hex(z.state == "active" ? C_TEXT : C_GRAU), 0);
        if (it.wo[WR_T_BAR] != nullptr) lv_bar_set_value(it.wo[WR_T_BAR], dauer > 0 ? (dauer - rest) * 1000 / dauer : 0, LV_ANIM_OFF);
        if (it.wo[WR_T_NAME] != nullptr && !*(it.j["label"] | "")) {
          auto n = z.attr.find(PStr("name"));
          std::string t = n != z.attr.end() ? n->second.c_str() : "Timer";
          if (z.state == "paused") t += " · Pause";
          else if (z.state == "idle") t += " · aus";
          lv_label_set_text(it.wo[WR_T_NAME], t.c_str());
        }
      }
    }
    if (it.wo[WR_M_BAR] != nullptr) {
      auto f = zustand_cache_.find(PStr(it.wart == W_KOMBI ? (it.j["me"] | "") : (it.j["entity"] | "")));
      bool zeig = false;
      if (f != zustand_cache_.end()) {
        const EntZustand &z = f->second;
        auto x = z.attr.find(PStr("media_position")), d = z.attr.find(PStr("media_duration"));
        float pos = x != z.attr.end() ? atof(x->second.c_str()) : -1, dur = d != z.attr.end() ? atof(d->second.c_str()) : 0;
        if (pos >= 0 && dur > 0) {
          if (z.state == "playing") pos += (millis() - z.pos_ms) / 1000.0f;
          lv_bar_set_value(it.wo[WR_M_BAR], (int) std::min(1000.0f, pos * 1000 / dur), LV_ANIM_OFF);
          zeig = true;
        }
      }
      if (zeig) lv_obj_remove_flag(it.wo[WR_M_BAR], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(it.wo[WR_M_BAR], LV_OBJ_FLAG_HIDDEN);
    }
    if (it.wart == W_KAMERA && it.wslot >= 0 && (it.wtakt_ms == 0 || (int32_t) (millis() - it.wtakt_ms) >= 0)) {
      const int takt = std::max(3, (int) (it.j["takt"] | 10));
      it.wtakt_ms = millis() + takt * 1000;
      if (it.wtakt_ms == 0) it.wtakt_ms = 1;
      widget_bild_(it, true);
    }
  }
}

void Menue::widget_bild_(Item &it, bool sofort) {
  if (it.wslot < 0 || !wbild_laden || tk_ == nullptr) return;
  const char *e = it.wart == W_KAMERA ? (it.j["entity"] | "") : it.wart == W_KOMBI ? (it.j["me"] | "") : (it.j["entity"] | "");
  if (!*e) return;
  int w, h;
  if (it.wart == W_KAMERA) { w = (int) (it.w * zelle_) - LUECKE - 4; h = it.h * ZEILE - LUECKE - 4; }
  else { w = h = it.h * ZEILE - LUECKE - 8; }   // Cover quadratisch
  char q[48];
  snprintf(q, sizeof(q), "&w=%d&h=%d&m=%s", w, h, it.wart == W_KAMERA && !strcmp(it.j["bild"] | "", "ganz") ? "ganz" : "voll");
  const std::string url = "http://" + tk_->host() + "/api/esphomeremote_konfig/bild?k=" + tk_->schluessel() + "&e=" + e + q;
  if (it.wart == W_KAMERA) g_wkey[it.wslot] = e;
  wbild_laden(it.wslot, url);
  (void) sofort;
}

void Menue::wbild_fertig(int slot) {
  if (!wbild || root_ == nullptr || lv_screen_active() != lv_obj_get_screen(root_)) return;
  const void *src = wbild(slot);
  if (src == nullptr) return;
  for (auto &it : items_) {
    if (it.typ != T_WIDGET || it.wslot != slot) continue;
    lv_obj_t *img = it.wart == W_KAMERA ? it.wo[WR_K_BILD] : it.wo[WR_M_BILD];
    if (img == nullptr) continue;
    lv_image_set_src(img, src);
    lv_obj_center(img);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    if (it.wo[WR_K_PH] != nullptr) lv_obj_add_flag(it.wo[WR_K_PH], LV_OBJ_FLAG_HIDDEN);
  }
}

void Menue::widget_ausloesen_(Item &it, int richtung) {
  const char *e = it.wart == W_KOMBI ? (it.j["me"] | "") : (it.j["entity"] | "");
  if (!*e) return;
  if (it.wart == W_TIMER) {
    auto f = zustand_cache_.find(PStr(e));
    const bool laeuft = f != zustand_cache_.end() && f->second.state == "active";
    tk_->ha_aufruf(laeuft ? "timer.pause" : "timer.start", {{"entity_id", e}});
  } else if (it.wart == W_MEDIEN || (it.wart == W_KOMBI && (it.wbau & (1 << W_MEDIEN)))) {
    const char *svc = richtung == -1 ? "media_player.media_previous_track" : richtung == 1 ? "media_player.media_next_track"
                                                                          : "media_player.media_play_pause";
    tk_->ha_aufruf(svc, {{"entity_id", e}});
  }
  abf_ab_ = millis() + 700;
}

void Menue::wknopf_cb_(lv_event_t *e) {
  auto *self = (Menue *) lv_event_get_user_data(e);
  const int v = (int) (intptr_t) lv_obj_get_user_data((lv_obj_t *) lv_event_get_current_target(e));
  const int i = v / 4, fn = v % 4;
  if (i < 0 || i >= (int) self->items_.size()) return;
  lv_event_stop_bubbling(e);
  self->widget_ausloesen_(self->items_[i], fn - 1);
}

}  // namespace menue
}  // namespace esphome
