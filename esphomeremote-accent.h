#pragma once
// Akzentfarbe der Remote (2026-11-01, Nutzer: "unser Blau ... ueber einen Color Picker im Konfigurator komplett
// aendern"). Konfigurator: Wurzel "akzent" ("#RRGGBB", fehlt = Blau 0x3B82F6).
//
// Die Oberflaeche ist mit festem Blau gebaut (YAML-Widgets, Theme, Stile, C++). Statt alles umzuschreiben:
//  - neu gebaute Teile holen die Farbe ueber akzent::farbe() (Kopf, Leiste, Touchpad, Gesten, Smart Display ...),
//  - beim Start und bei jeder neuen Konfiguration faerbt akzent::anwenden() alle vorhandenen Objekte um, deren
//    eigene Farbe ein Blau der Oberflaeche ist (Akzent und seine hellen/dunklen Abstufungen).
// Ausgenommen sind die Anzeigen, deren Blau "Bluetooth" bedeutet (Balken, HA ueber Bluetooth, Dock-Weg, Google);
// das Bewegungs-Symbol folgt der Akzentfarbe (Nutzerwunsch).
#include <vector>

namespace akzent {

static const uint32_t BLAU = 0x3B82F6;

inline uint32_t &wert() { static uint32_t w = BLAU; return w; }
inline uint32_t farbe() { return wert(); }

// c Richtung z mischen (p von 255)
inline uint32_t mischen(uint32_t c, uint32_t z, int p) {
  auto k = [&](int s) { const int a = (c >> s) & 0xFF, b = (z >> s) & 0xFF; return (uint32_t) (a + (b - a) * p / 255) << s; };
  return k(16) | k(8) | k(0);
}
// Abstufungen wie beim Blau: gedrueckt dunkler, Rand heller, Flaechen sehr dunkel
struct Stufen { uint32_t f[5]; };
inline Stufen stufen(uint32_t a) {
  if (a == BLAU) return {{0x3B82F6, 0x2563EB, 0x60A5FA, 0x1E3A8A, 0x93C5FD}};
  return {{a, mischen(a, 0x000000, 50), mischen(a, 0xFFFFFF, 70), mischen(a, 0x000000, 150), mischen(a, 0xFFFFFF, 130)}};
}

inline std::vector<lv_obj_t *> &ausnahmen() { static std::vector<lv_obj_t *> v; return v; }
inline bool ausgenommen(lv_obj_t *o) {
  for (auto *x : ausnahmen()) if (x == o) return true;
  return false;
}

inline lv_color_t hex(uint32_t c) { return lv_color_hex(c); }
inline uint32_t als_hex(lv_color_t c) { return lv_color_to_u32(c) & 0xFFFFFF; }

inline void faerben_(lv_obj_t *o, const Stufen &alt, const Stufen &neu, bool knopf_theme) {
  if (o == nullptr) return;
  if (!ausgenommen(o)) {
    static const lv_style_prop_t PROPS[] = {LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_TEXT_COLOR, LV_STYLE_OUTLINE_COLOR,
                                            LV_STYLE_LINE_COLOR, LV_STYLE_ARC_COLOR, LV_STYLE_SHADOW_COLOR, LV_STYLE_BG_GRAD_COLOR,
                                            LV_STYLE_IMAGE_RECOLOR};
    static const lv_part_t TEILE[] = {LV_PART_MAIN, LV_PART_SCROLLBAR, LV_PART_INDICATOR, LV_PART_KNOB, LV_PART_SELECTED, LV_PART_ITEMS, LV_PART_CURSOR};
    static const lv_state_t ZUST[] = {LV_STATE_DEFAULT, LV_STATE_CHECKED, LV_STATE_FOCUSED, LV_STATE_FOCUS_KEY, LV_STATE_PRESSED,
                                      LV_STATE_EDITED, (lv_state_t) (LV_STATE_CHECKED | LV_STATE_PRESSED)};
    for (lv_part_t t : TEILE)
      for (lv_state_t z : ZUST) {
        const lv_style_selector_t sel = (lv_style_selector_t) t | z;
        for (lv_style_prop_t p : PROPS) {
          lv_style_value_t v;
          if (lv_obj_get_local_style_prop(o, p, &v, sel) != LV_RESULT_OK) continue;
          const uint32_t c = als_hex(v.color);
          for (int i = 0; i < 5; i++)
            if (c == alt.f[i] && c != neu.f[i]) {
              v.color = hex(neu.f[i]);
              lv_obj_set_local_style_prop(o, p, v, sel);
              break;
            }
        }
      }
    // Theme-Knoepfe (Fokus-Rahmen, gedrueckt): eigene Werte setzen, die das Theme ueberdecken
    if (knopf_theme && lv_obj_check_type(o, &lv_button_class)) {
      lv_style_value_t v;
      if (lv_obj_get_local_style_prop(o, LV_STYLE_BORDER_COLOR, &v, LV_STATE_FOCUSED) != LV_RESULT_OK)
        lv_obj_set_style_border_color(o, hex(neu.f[0]), LV_STATE_FOCUSED);
      if (lv_obj_get_local_style_prop(o, LV_STYLE_BG_COLOR, &v, LV_STATE_PRESSED) != LV_RESULT_OK)
        lv_obj_set_style_bg_color(o, hex(neu.f[1]), LV_STATE_PRESSED);
      if (lv_obj_get_local_style_prop(o, LV_STYLE_BORDER_COLOR, &v, LV_STATE_PRESSED) != LV_RESULT_OK)
        lv_obj_set_style_border_color(o, hex(neu.f[2]), LV_STATE_PRESSED);
    }
  }
  const uint32_t n = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < n; i++) faerben_(lv_obj_get_child(o, i), alt, neu, knopf_theme);
}

// Gemeinsame Stile (style_definitions, z. B. st_back = Zurueck/X auf Tastatur, Ziffern, Kamera, Media Player ...):
// die liegen nicht als eigene Werte am Objekt und blieben sonst blau (2026-11-01, Nutzer).
inline void stil_faerben_(lv_style_t *s, const Stufen &alt, const Stufen &neu) {
  static const lv_style_prop_t PROPS[] = {LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR, LV_STYLE_TEXT_COLOR, LV_STYLE_OUTLINE_COLOR,
                                          LV_STYLE_LINE_COLOR, LV_STYLE_ARC_COLOR, LV_STYLE_SHADOW_COLOR, LV_STYLE_BG_GRAD_COLOR,
                                          LV_STYLE_IMAGE_RECOLOR};
  bool geaendert = false;
  for (lv_style_prop_t p : PROPS) {
    lv_style_value_t v;
    if (lv_style_get_prop(s, p, &v) != LV_STYLE_RES_FOUND) continue;
    const uint32_t c = als_hex(v.color);
    for (int i = 0; i < 5; i++)
      if (c == alt.f[i] && c != neu.f[i]) {
        v.color = hex(neu.f[i]);
        lv_style_set_prop(s, p, v);
        geaendert = true;
        break;
      }
  }
  if (geaendert) lv_obj_report_style_change(s);
}

// Alle Bildschirme (ESPHome-Seiten) und die Ebenen darueber umfaerben. seiten: alle Seiten-Objekte, stile: gemeinsame Stile.
inline void anwenden(uint32_t neu_wert, const std::vector<lv_obj_t *> &seiten, const std::vector<lv_style_t *> &stile = {}) {
  const uint32_t alt_wert = wert();
  if (alt_wert == neu_wert) return;
  const Stufen alt = stufen(alt_wert), neu = stufen(neu_wert);
  const uint32_t t0 = millis();
  for (auto *s : seiten) faerben_(s, alt, neu, neu_wert != BLAU);
  for (auto *s : stile) stil_faerben_(s, alt, neu);
  faerben_(lv_layer_top(), alt, neu, neu_wert != BLAU);
  wert() = neu_wert;
  lv_obj_invalidate(lv_screen_active());
  ESP_LOGI("akzent", "Akzentfarbe #%06X (vorher #%06X) in %u ms", (unsigned) neu_wert, (unsigned) alt_wert, (unsigned) (millis() - t0));
}

// "#RRGGBB" aus der Konfiguration, sonst Blau
inline uint32_t aus_text(const char *t) {
  if (t == nullptr || t[0] != '#' || strlen(t) != 7) return BLAU;
  char *ende = nullptr;
  const unsigned long v = strtoul(t + 1, &ende, 16);
  return (ende != nullptr && *ende == 0) ? (uint32_t) v : BLAU;
}

}  // namespace akzent
