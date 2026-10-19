// Seite "Gesten" der Remote (2026-10-17, page_gesten, cur_page 45, Menue-Spezialseite "gesten"):
//   Liste:   Gesten an/aus, bei dunklem Display, Empfindlichkeit; jede Geste einzeln (an/aus, Belegung);
//            "+ Neue Geste anlernen" (Name ueber die Bildschirmtastatur).
//   Geste:   aktiv, "wirkt wie Taste" (kurzer Druck dieser Taste in der Aktivitaet), neu anlernen,
//            umbenennen, loeschen. Weitere Aktionen (HA-Dienste usw.) im Konfigurator unter Fernbedienung > Gesten.
//   Anlernen: dreimal die Geste ausfuehren, jedes Mal mit OK bestaetigen (Zurueck = verwerfen). Danach prueft die
//            Remote, ob die Proben zusammenpassen (Form-Vergleich wie bei der Erkennung, Kreise nach Ebene und
//            Drehrichtung) und ob sie einer anderen Geste zu aehnlich sind, und schickt sie an HA
//            (Ereignis esphome.remote_gesten_aendern, aktion "lernen", wert "2" = Format Lage-Rahmen).
//            Auch aus dem Konfigurator startbar (Dienst geste_anlernen) - Fortschritt: esphome.remote_geste_lernstand.
// Aenderungen speichert HA in cfg.gesten; die neue Belegung kommt wie immer per Hash-Ankuendigung zurueck.
#pragma once
#include <array>
#include <map>
#include <set>

namespace gestenseite {

enum Ansicht { LISTE, GESTE, LERNEN };

struct Eintrag {
  std::string key, name;   // key: schuetteln / kipp_l / kipp_r oder Kennung der eigenen Geste
  bool eigen{false}, an{true}, belegt{false}, alt{false};   // alt: Vorlagen im alten Format -> neu anlernen
  int taste{0}, aktionen{0}, proben{0};
};

struct Lern {
  std::string id, name;
  std::vector<std::array<int8_t, 96>> proben;
  std::array<int8_t, 96> kandidat{};
  uint32_t dauer{0};
  int phase{0};      // 0 = wartet auf Bewegung, 1 = Probe pruefen (OK/Zurueck), 2 = Rueckfrage (aehnlich), 3 = gesendet
  int ersetzen{-1};  // Probe, die nochmal gemacht werden soll
  std::string meldung;
  bool kreis{false};
};

struct Zustand {
  Ansicht ansicht{LISTE};
  bool behalten{false};       // naechstes Laden der Seite: Ansicht behalten (Ruecksprung von der Tastatur)
  std::string sel;            // gewaehlte Geste
  std::vector<lv_obj_t *> fokus;
  int fi{0};
  uint32_t gen{0xFFFFFFFF};
  // Sofort sichtbare Aenderungen, bis HA die neue Belegung schickt
  std::map<std::string, int> an_neu, taste_neu;
  std::map<std::string, std::string> name_neu;
  std::set<std::string> weg;
  bool loeschen_frage{false};
  Lern lern;
  // Namenseingabe ueber die Bildschirmtastatur (Seite 41)
  bool eingabe{false};
  int eingabe_zweck{0};       // 1 = neue Geste, 2 = umbenennen
  std::string text;
  // Aufbau
  lv_obj_t *ziel{nullptr};    // Eltern fuer zeile()/knopf() (sonst gest_box)
  lv_obj_t *lbl_gross{nullptr}, *lbl_klein{nullptr};
  int bereit{-1};             // Live-Anzeige beim Anlernen: 0 ruhig halten, 1 jetzt Geste, 2 Bewegung laeuft
};
inline Zustand &z() { static Zustand s; return s; }

static const struct { const char *k; const char *n; } FEST[] = {
  {"schuetteln", "Zweimal schütteln"}, {"kipp_l", "Kippen links"}, {"kipp_r", "Kippen rechts"},
};
// Tasten fuer "wirkt wie Taste" (Reihenfolge beim Weiterschalten)
static const struct { int k; const char *n; } TASTEN[] = {
  {0, "keine"}, {2, "Play/Pause"}, {5, "Stop"}, {4, "Rücklauf"}, {11, "Vorlauf"}, {33, "Lauter"}, {43, "Leiser"},
  {44, "Stumm"}, {42, "Kanal +"}, {22, "Kanal −"}, {102, "Aus"}, {3, "Menu"}, {31, "Info"}, {34, "OK"},
  {13, "Zurück"}, {23, "Rot"}, {25, "Grün"}, {24, "Gelb"}, {21, "Blau"}, {45, "Mikrofon"},
};
inline const char *taste_name(int k) {
  for (const auto &t : TASTEN) if (t.k == k) return t.n;
  return "Taste";
}

// ---------------------------------------------------------------- Daten
inline std::vector<Eintrag> eintraege() {
  auto &s = z();
  std::vector<Eintrag> out;
  JsonObjectConst g = tk_cfg->konfig("gesten");
  auto fuellen = [&](Eintrag &e, JsonObjectConst o) {
    if (!o.isNull()) {
      e.an = o["an"] | true;
      e.taste = o["k"] | 0;
      e.aktionen = o["s"].size();
    }
    if (s.an_neu.count(e.key)) e.an = s.an_neu[e.key];
    if (s.taste_neu.count(e.key)) e.taste = s.taste_neu[e.key];
    if (s.name_neu.count(e.key)) e.name = s.name_neu[e.key];
    e.belegt = e.taste > 0 || e.aktionen > 0;
  };
  for (const auto &f : FEST) {
    Eintrag e;
    e.key = f.k; e.name = f.n;
    JsonObjectConst o = g[f.k];
    e.an = !o.isNull();   // nie eingerichtet = aus
    fuellen(e, o);
    out.push_back(e);
  }
  for (JsonObjectConst o : g["eigen"].as<JsonArrayConst>()) {
    Eintrag e;
    e.key = o["id"] | ""; e.name = o["n"] | ""; e.eigen = true;
    e.proben = o["v"].size();
    e.alt = e.proben > 0 && (o["f"] | 1) != 2;
    if (e.key.empty() || s.weg.count(e.key)) continue;
    fuellen(e, o);
    out.push_back(e);
  }
  return out;
}

inline bool finden(const std::string &k, Eintrag &e) {
  for (auto &x : eintraege()) if (x.key == k) { e = x; return true; }
  return false;
}

// ---------------------------------------------------------------- an HA
inline void aendern(const char *aktion, const std::string &id, const std::string &name = "", const std::string &wert = "",
                    const std::string &v = "") {
  ESP_LOGI("geste", "Aenderung an HA: %s %s %s %s", aktion, id.c_str(), name.c_str(), wert.c_str());
  gesten_aendern_an_ha->execute(std::string(aktion), id, name, wert, v);
}

inline void lernstand(const std::string &text) {
  auto &l = z().lern;
  geste_lernstand_an_ha->execute(l.id, (int) l.proben.size(), text);
}

// ---------------------------------------------------------------- Aufbau
inline void klick_cb(lv_event_t *e);

inline lv_obj_t *zeile(const char *name, const char *wert, uint32_t farbe, int tag, bool gross = false) {
  lv_obj_t *b = lv_button_create(z().ziel ? z().ziel : gest_box);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(b, gross ? 52 : 44, 0);
  lv_obj_set_style_pad_ver(b, 6, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x232A35), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x2563EB), LV_STATE_PRESSED);
  lv_obj_set_style_radius(b, 11, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0x232A35), 0);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 8, 0);
  lv_obj_t *n = lv_label_create(b);
  lv_label_set_text(n, name);
  lv_obj_set_style_text_font(n, a15->get_lv_font(), 0);
  lv_obj_set_style_text_color(n, lv_color_hex(0xF1F3F5), 0);
  lv_obj_set_width(n, 10);        // Grundbreite klein, flex_grow fuellt: lange Namen brechen an Wortgrenzen um
  lv_obj_set_flex_grow(n, 1);
  lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
  if (wert != nullptr && *wert) {
    lv_obj_t *w = lv_label_create(b);
    lv_label_set_text(w, wert);
    lv_obj_set_style_text_font(w, a13->get_lv_font(), 0);
    lv_obj_set_style_text_color(w, lv_color_hex(farbe), 0);
    lv_obj_set_style_max_width(w, 110, 0);
    lv_obj_set_height(w, 18);
    lv_label_set_long_mode(w, LV_LABEL_LONG_DOT);
  }
  lv_obj_add_event_cb(b, klick_cb, LV_EVENT_CLICKED, (void *) (intptr_t) tag);
  z().fokus.push_back(b);
  return b;
}

inline lv_obj_t *knopf(const char *text, int tag, uint32_t farbe = 0x232A35) {
  lv_obj_t *b = zeile(text, nullptr, 0, tag);
  lv_obj_set_style_bg_color(b, lv_color_hex(farbe), 0);
  lv_obj_set_style_border_color(b, lv_color_hex(farbe), 0);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *n = lv_obj_get_child(b, 0);
  lv_obj_set_flex_grow(n, 0);
  lv_obj_set_width(n, LV_SIZE_CONTENT);
  lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(n, 190, 0);
  return b;
}

inline lv_obj_t *notiz(const std::string &t, uint32_t farbe = 0x8B94A3, const lv_font_t *f = nullptr) {
  lv_obj_t *l = lv_label_create(gest_box);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l, t.c_str());
  lv_obj_set_style_text_font(l, f ? f : a13->get_lv_font(), 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  return l;
}

inline void kopf(const char *t) {
  lv_obj_t *l = notiz(t, 0x3B82F6);
  lv_obj_set_style_pad_top(l, 6, 0);
}

inline void fokus_zeigen() {
  auto &s = z();
  if (s.fokus.empty()) return;
  s.fi = std::max(0, std::min((int) s.fokus.size() - 1, s.fi));
  for (size_t i = 0; i < s.fokus.size(); i++) {
    lv_obj_t *b = s.fokus[i];
    const bool f = (int) i == s.fi;
    lv_obj_set_style_border_color(b, f ? lv_color_hex(0x3B82F6) : lv_obj_get_style_bg_color(b, LV_PART_MAIN), 0);
  }
  lv_obj_scroll_to_view(s.fokus[s.fi], LV_ANIM_ON);
}

enum Tag { T_AN = 1, T_DUNKEL, T_SENS, T_NEU, T_HILFE,
           T_AKTIV = 20, T_TASTE, T_LERNEN, T_NAME, T_LOESCHEN,
           T_OK = 40, T_NOCHMAL, T_ABBRUCH,
           T_GESTE = 100 };

inline void zeichnen() {
  auto &s = z();
  lv_obj_clean(gest_box);
  s.lbl_gross = s.lbl_klein = nullptr;
  // Spalte (im YAML ginge flex nur mit widgets darunter)
  lv_obj_set_flex_flow(gest_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(gest_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(gest_box, 8, 0);
  s.fokus.clear();
  char b[64];
  if (s.ansicht == LISTE) {
    lv_label_set_text(gest_titel, "Gesten");
    zeile("Gesten", gesten_an->value() ? "AN" : "AUS", gesten_an->value() ? 0x22C55E : 0x8B94A3, T_AN);
    zeile("Bei dunklem Display", gesten_dunkel->value() ? "AN" : "AUS", gesten_dunkel->value() ? 0x22C55E : 0x8B94A3, T_DUNKEL);
    static const char *SENS[] = {"Streng", "Normal", "Empfindlich"};   // wie im Konfigurator
    zeile("Empfindlichkeit", SENS[std::max(0, std::min(2, (int) gesten_sens->value()))], 0xF1F3F5, T_SENS);
    if (!gesten_an->value()) notiz("Gesten sind aus - einzelne Gesten wirken erst, wenn „Gesten“ an ist.");
    kopf("GESTEN");
    const auto liste = eintraege();
    for (size_t i = 0; i < liste.size(); i++) {
      const auto &e = liste[i];
      std::string w;
      if (e.alt) w = "neu anlernen";
      else if (!e.an) w = "AUS";
      else if (e.taste > 0) w = taste_name(e.taste);
      else if (e.aktionen > 0) { snprintf(b, sizeof(b), "%d Aktion%s", e.aktionen, e.aktionen > 1 ? "en" : ""); w = b; }
      else w = "frei";
      zeile(e.name.c_str(), w.c_str(), e.alt ? 0xFACC15 : !e.an ? 0x8B94A3 : e.belegt ? 0x22C55E : 0xFACC15, T_GESTE + (int) i);
    }
    // HA nimmt hoechstens 8 eigene Gesten an - vorher lief das Anlernen durch und die Geste fehlte danach
    int eigene = 0;
    for (const auto &e : liste) if (e.eigen) eigene++;
    if (eigene < 8) knopf("+ Neue Geste anlernen", T_NEU, 0x1E3A8A);
    else notiz("8 eigene Gesten - mehr gehen nicht. Erst eine löschen.", 0xFACC15);
    notiz("Antippen zum Einstellen. Eigene Gesten: Name eingeben, dann dreimal ausführen und jedes Mal mit OK bestätigen. "
          "Erkannt wird die Form: Größe, Tempo und Haltung der Remote sind egal, ein Kreis zählt in jeder Größe "
          "(Drehrichtung zählt). Schutz gegen Fehlauslösen: nur nach kurzer Ruhe, nicht im Dock/am Kabel, nicht direkt nach einer Taste.");
  } else if (s.ansicht == GESTE) {
    Eintrag e;
    if (!finden(s.sel, e)) { s.ansicht = LISTE; zeichnen(); return; }
    lv_label_set_text(gest_titel, e.name.c_str());
    zeile("Aktiv", e.an ? "AN" : "AUS", e.an ? 0x22C55E : 0x8B94A3, T_AKTIV);
    zeile("Wirkt wie Taste", taste_name(e.taste), e.taste > 0 ? 0x22C55E : 0x8B94A3, T_TASTE);
    if (e.aktionen > 0) snprintf(b, sizeof(b), "Dazu %d Aktion%s aus dem Konfigurator.", e.aktionen, e.aktionen > 1 ? "en" : "");
    else snprintf(b, sizeof(b), "Mehr Aktionen: Konfigurator > Gesten.");   // vorher 69 Zeichen in 64 - abgeschnitten
    notiz(std::string("OK schaltet die Taste weiter. Die Geste wirkt dann wie ein kurzer Druck dieser Taste in der aktuellen Aktivität. ") + b);
    if (e.alt) notiz("Diese Geste wurde mit der alten Erkennung angelernt und wirkt erst nach „Neu anlernen“.", 0xFACC15);
    if (e.eigen) {
      snprintf(b, sizeof(b), "%d Probe%s gespeichert", e.proben, e.proben == 1 ? "" : "n");
      zeile("Neu anlernen", b, 0x8B94A3, T_LERNEN);
      zeile("Umbenennen", nullptr, 0, T_NAME);
      zeile(s.loeschen_frage ? "Wirklich löschen? OK = ja" : "Geste löschen", nullptr, 0, T_LOESCHEN);
      if (s.loeschen_frage) lv_obj_set_style_bg_color(s.fokus.back(), lv_color_hex(0x7F1D1D), 0);
    } else {
      notiz(s.sel == "schuetteln" ? "Zweimal kurz hin und her schütteln (mind. 3 Richtungswechsel in 1,5 s)."
                                  : "Remote seitlich kippen (über 50°) und eine halbe Sekunde halten. Erst nach dem Zurückkippen wieder bereit.");
    }
  } else {
    auto &l = s.lern;
    snprintf(b, sizeof(b), "Anlernen: %s", l.name.c_str());
    lv_label_set_text(gest_titel, b);
    // drei Punkte als Fortschritt
    lv_obj_t *reihe = lv_obj_create(gest_box);
    lv_obj_remove_style_all(reihe);
    lv_obj_set_size(reihe, lv_pct(100), 22);
    lv_obj_set_flex_flow(reihe, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reihe, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(reihe, 14, 0);
    for (int i = 0; i < 3; i++) {
      lv_obj_t *p = lv_obj_create(reihe);
      lv_obj_remove_style_all(p);
      lv_obj_set_size(p, 16, 16);
      lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
      const bool fertig = i < (int) l.proben.size() && i != l.ersetzen;
      const bool jetzt = l.phase <= 1 && (l.ersetzen >= 0 ? i == l.ersetzen : i == (int) l.proben.size());
      lv_obj_set_style_bg_color(p, lv_color_hex(fertig ? 0x22C55E : jetzt ? 0x3B82F6 : 0x3A4250), 0);
      lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    }
    const int nr = (l.ersetzen >= 0 ? l.ersetzen : (int) l.proben.size()) + 1;
    std::string gross, klein;
    if (l.phase == 0) {
      snprintf(b, sizeof(b), "Probe %d von 3", std::min(nr, 3));
      gross = b;
      klein = "Ruhig halten ...";   // Live-Anzeige in takt()
    } else if (l.phase == 1) {
      snprintf(b, sizeof(b), "Bewegung erkannt (%.1f s)", l.dauer / 1000.0f);
      gross = b;
      klein = "Diese Bewegung übernehmen?";
    } else if (l.phase == 2) {
      gross = "Achtung";
      klein = "Trotzdem speichern?";
    } else {
      gross = "Gespeichert";
      klein = "Die Geste ist gleich aktiv.";
    }
    s.lbl_gross = notiz(gross, 0xF1F3F5, a18->get_lv_font());
    lv_obj_set_style_text_align(s.lbl_gross, LV_TEXT_ALIGN_CENTER, 0);
    s.lbl_klein = notiz(klein, l.phase == 0 ? 0xFACC15 : 0x8B94A3);
    lv_obj_set_style_text_align(s.lbl_klein, LV_TEXT_ALIGN_CENTER, 0);
    s.bereit = -1;
    if (!l.meldung.empty()) {
      lv_obj_t *m = notiz(l.meldung, l.phase == 3 ? 0x22C55E : 0xFACC15);
      lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    }
    // Knoepfe nebeneinander (passt ohne Scrollen)
    lv_obj_t *kr = lv_obj_create(gest_box);
    lv_obj_remove_style_all(kr);
    lv_obj_set_size(kr, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(kr, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(kr, 8, 0);
    lv_obj_set_style_pad_top(kr, 4, 0);
    s.ziel = kr;
    auto halb = [](lv_obj_t *k) { lv_obj_set_width(k, 0); lv_obj_set_flex_grow(k, 1); };
    if (l.phase == 1 || l.phase == 2) {
      halb(knopf(l.phase == 1 ? "Übernehmen" : "Speichern", T_OK, 0x1E3A8A));
      halb(knopf(l.phase == 1 ? "Verwerfen" : "Neu", T_NOCHMAL));
    } else if (l.phase == 3) {
      halb(knopf("Zur Liste", T_OK, 0x1E3A8A));
    } else {
      halb(knopf("Abbrechen", T_ABBRUCH));
    }
    s.ziel = nullptr;
    if (l.phase == 0)
      notiz("OK erst nach der Bewegung. Zurück = abbrechen.", 0x5B6472);
    else if (l.phase == 1)
      notiz("OK = übernehmen · Zurück = verwerfen", 0x5B6472);
  }
  fokus_zeigen();
}

// ---------------------------------------------------------------- Anlernen
inline void lern_ende() {
  gesten::z().lernen = false;
}

inline void lern_starten(const std::string &id, const std::string &name) {
  auto &s = z();
  s.lern = Lern{};
  s.lern.id = id;
  s.lern.name = name.empty() ? std::string("Geste") : name;
  s.ansicht = LERNEN;
  s.sel = id;
  s.fi = 0;
  auto &g = gesten::z();
  g.lernen = true;
  g.in_bewegung = false; g.ruhig = 0;
  wake_backlight->execute();
  if (cur_page->value() != 45) { s.behalten = true; tasten::seite_zeigen(page_gesten, LV_SCR_LOAD_ANIM_NONE); }
  else zeichnen();
  lernstand("Probe 1 von 3: Remote ruhig halten, Geste ausführen, dann OK.");
  ESP_LOGI("geste", "Anlernen: %s (%s)", s.lern.name.c_str(), id.c_str());
}

inline std::string neue_id() {
  char b[12];
  snprintf(b, sizeof(b), "g%06x", (unsigned) ((esp_random() ^ millis()) & 0xFFFFFF));
  return b;
}

inline void speichern() {
  auto &l = z().lern;
  std::string v;
  for (const auto &p : l.proben) {
    char hex[193];
    for (int i = 0; i < 96; i++) snprintf(hex + 2 * i, 3, "%02x", (uint8_t) p[i]);
    if (!v.empty()) v += ",";
    v += hex;
  }
  aendern("lernen", l.id, l.name, "2", v);
  l.phase = 3;
  l.meldung = l.kreis ? "Erkannt als Kreis: jeder Kreis in dieser Richtung zählt - groß, klein, schnell oder langsam."
                      : "Gespeichert als Bewegungsform: Größe und Tempo dürfen abweichen.";
  lern_ende();
  lernstand("Gespeichert");
}

// Drei Proben pruefen: passen sie zueinander, ist eine Ausreisser, aehnelt die Geste einer anderen?
// Wie die Erkennung: Form statt 1:1 (Lage-Rahmen, Drehung um die Hochachse, Kreise nach Ebene + Drehrichtung).
inline void auswerten() {
  auto &l = z().lern;
  gesten::Eigene g;
  for (const auto &p : l.proben) { gesten::Vorlage v; memcpy(v.w, p.data(), 96); g.vorlagen.push_back(v); }
  gesten::gruppe(g);
  l.kreis = g.kreis;
  const float d01 = gesten::abstand(l.proben[0].data(), l.proben[1].data());
  const float d02 = gesten::abstand(l.proben[0].data(), l.proben[2].data());
  const float d12 = gesten::abstand(l.proben[1].data(), l.proben[2].data());
  const float grenze = 24.0f;   // grosszuegig: die Erkennung passt ihre Grenze an die Streuung an
  ESP_LOGI("geste", "Anlernen: Abstaende %.1f %.1f %.1f%s", d01, d02, d12, g.kreis ? " (Kreis)" : "");
  char b[140];
  if (!g.kreis && std::max(d01, std::max(d02, d12)) > grenze) {
    const float s0 = d01 + d02, s1 = d01 + d12, s2 = d02 + d12;
    const int w = s0 >= s1 && s0 >= s2 ? 0 : s1 >= s2 ? 1 : 2;
    const float rest = w == 0 ? d12 : w == 1 ? d02 : d01;
    if (rest <= grenze) {
      l.ersetzen = w;
      snprintf(b, sizeof(b), "Probe %d weicht ab - bitte diese nochmal.", w + 1);
      l.meldung = b;
    } else {
      l.proben.clear();
      l.ersetzen = -1;
      l.meldung = "Die Proben passen nicht zusammen - nochmal von vorn.";
    }
    l.phase = 0;
    lernstand(l.meldung);
    return;
  }
  // Verwechslungsgefahr mit einer anderen eigenen Geste
  gesten::vorlagen_laden();
  std::string aehnlich;
  float best = 1e9f;
  for (const auto &p : l.proben) {
    const gesten::Treffer t = gesten::vergleichen(p.data(), l.id);
    if (t.beste >= 0 && t.w1 < best) { best = t.w1; aehnlich = gesten::z().eigene[t.beste].name; }
  }
  if (best <= 1.0f) {
    snprintf(b, sizeof(b), "Sehr ähnlich wie „%s“ - könnte verwechselt werden.", aehnlich.c_str());
    l.meldung = b;
    l.phase = 2;
    lernstand(l.meldung);
    return;
  }
  speichern();
}

inline void probe(const int8_t *w, uint32_t dauer) {
  auto &s = z();
  auto &l = s.lern;
  if (s.ansicht != LERNEN || l.phase != 0) return;
  memcpy(l.kandidat.data(), w, 96);
  l.dauer = dauer;
  l.phase = 1;
  l.meldung = "";
  s.fi = 0;
  wake_backlight->execute();
  if (cur_page->value() == 45) zeichnen();
  lernstand("Bewegung erkannt - mit OK übernehmen.");
}

inline void lern_ok() {
  auto &l = z().lern;
  if (l.phase == 1) {
    if (l.ersetzen >= 0 && l.ersetzen < (int) l.proben.size()) l.proben[l.ersetzen] = l.kandidat;
    else l.proben.push_back(l.kandidat);
    l.ersetzen = -1;
    l.phase = 0;
    l.meldung = "";
    if (l.proben.size() >= 3) auswerten();
    else {
      char b[60];
      snprintf(b, sizeof(b), "Probe %d von 3: Geste nochmal ausführen, dann OK.", (int) l.proben.size() + 1);
      lernstand(b);
    }
  } else if (l.phase == 2) {
    speichern();
  } else if (l.phase == 3) {
    z().ansicht = LISTE;
  }
}

inline void lern_zurueck() {
  auto &s = z();
  auto &l = s.lern;
  if (l.phase == 1) { l.phase = 0; l.meldung = "Verworfen - nochmal ausführen."; lernstand(l.meldung); return; }
  if (l.phase == 2) { l.proben.clear(); l.phase = 0; l.meldung = ""; lernstand("Neu: Probe 1 von 3"); return; }
  // abbrechen
  if (l.phase != 3) lernstand("Abgebrochen");
  lern_ende();
  Eintrag e;
  s.ansicht = finden(l.id, e) ? GESTE : LISTE;
}

// ---------------------------------------------------------------- Bedienung
inline void namen_eingeben(int zweck, const std::string &vorgabe) {
  auto &s = z();
  s.eingabe = true;
  s.eingabe_zweck = zweck;
  s.text = vorgabe;
  tasten::spezial_zeigen("tastatur");
}

inline void aktion(int tag) {
  auto &s = z();
  if (tag != T_LOESCHEN) s.loeschen_frage = false;
  if (tag >= T_GESTE) {
    const auto l = eintraege();
    const int i = tag - T_GESTE;
    if (i < (int) l.size()) { s.sel = l[i].key; s.ansicht = GESTE; s.fi = 0; }
  } else switch (tag) {
    case T_AN: gesten_an->value() = !gesten_an->value(); break;
    case T_DUNKEL: gesten_dunkel->value() = !gesten_dunkel->value(); break;
    case T_SENS: gesten_sens->value() = (gesten_sens->value() + 1) % 3; break;
    case T_NEU: namen_eingeben(1, ""); return;
    case T_AKTIV: {
      Eintrag e;
      if (!finden(s.sel, e)) break;
      s.an_neu[s.sel] = !e.an;
      aendern("an", s.sel, "", e.an ? "0" : "1");
      break;
    }
    case T_TASTE: {
      Eintrag e;
      if (!finden(s.sel, e)) break;
      const int n = sizeof(TASTEN) / sizeof(TASTEN[0]);
      int i = 0;
      for (int j = 0; j < n; j++) if (TASTEN[j].k == e.taste) i = j;
      const int k = TASTEN[(i + 1) % n].k;
      s.taste_neu[s.sel] = k;
      if (!e.an && k > 0) { s.an_neu[s.sel] = 1; aendern("an", s.sel, "", "1"); }
      aendern("taste", s.sel, "", std::to_string(k));
      break;
    }
    case T_LERNEN: {
      Eintrag e;
      if (finden(s.sel, e)) lern_starten(e.key, e.name);
      return;
    }
    case T_NAME: {
      Eintrag e;
      if (finden(s.sel, e)) namen_eingeben(2, e.name);
      return;
    }
    case T_LOESCHEN:
      if (!s.loeschen_frage) { s.loeschen_frage = true; break; }
      s.loeschen_frage = false;
      s.weg.insert(s.sel);
      aendern("loeschen", s.sel);
      s.ansicht = LISTE;
      s.fi = 0;
      break;
    case T_OK: lern_ok(); break;
    case T_NOCHMAL: lern_zurueck(); break;
    case T_ABBRUCH: s.lern.phase = 0; lern_zurueck(); break;
    default: break;
  }
  zeichnen();
}

inline void klick_cb(lv_event_t *e) {
  const int tag = (int) (intptr_t) lv_event_get_user_data(e);
  auto &s = z();
  for (size_t i = 0; i < s.fokus.size(); i++) if (s.fokus[i] == lv_event_get_target(e)) s.fi = i;
  last_input_ms->value() = millis();
  aktion(tag);
}

// D-Pad/OK/Zurueck auf der Seite (aus dispatch_key). false = normale Navigation (Zurueck aus der Liste).
inline bool taste(int keycode) {
  auto &s = z();
  switch (keycode) {
    case 14: s.fi--; fokus_zeigen(); return true;
    case 35: s.fi++; fokus_zeigen(); return true;
    case 15: case 32:
      if (s.ansicht == LERNEN) { s.fi += keycode == 32 ? 1 : -1; fokus_zeigen(); }
      return true;
    case 34:
      if (s.ansicht == LERNEN) {   // OK = bestaetigen; beim Warten auf die Bewegung ohne Wirkung
        if (s.lern.phase != 0) aktion(T_OK);
        return true;
      }
      if (s.fi >= 0 && s.fi < (int) s.fokus.size()) lv_obj_send_event(s.fokus[s.fi], LV_EVENT_CLICKED, nullptr);
      return true;
    case 13:
      if (s.ansicht == LERNEN) { lern_zurueck(); zeichnen(); return true; }
      if (s.ansicht == GESTE) { s.ansicht = LISTE; s.loeschen_frage = false; s.fi = 0; zeichnen(); return true; }
      return false;
    default: return false;
  }
}

// Kopfzeile wie auf den anderen Einstellungsseiten (focus_apply macht das dort): schwebend ganz oben,
// volle Breite, blaue Linie mit dem Zurueck-Feld (tasten::kopf_linie)
inline void kopf_einrichten() {
  lv_obj_t *hd = lv_obj_get_parent(back_gesten), *sc = lv_obj_get_parent(hd);
  if (lv_obj_has_flag(hd, LV_OBJ_FLAG_FLOATING)) return;
  lv_obj_add_flag(hd, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_width(hd, 240);
  lv_obj_set_height(hd, 44);
  lv_obj_align(hd, LV_ALIGN_TOP_LEFT, -lv_obj_get_style_pad_left(sc, LV_PART_MAIN), -lv_obj_get_style_pad_top(sc, LV_PART_MAIN));
  lv_obj_set_style_pad_left(hd, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_right(hd, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_top(hd, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(hd, 8, LV_PART_MAIN);
  lv_obj_move_foreground(hd);
  tasten::kopf_linie(hd, back_gesten, 0x0E1116);
}

// page_gesten on_load
inline void geladen() {
  auto &s = z();
  kopf_einrichten();
  if (!s.behalten && s.ansicht != LERNEN) { s.ansicht = LISTE; s.fi = 0; }
  s.behalten = false;
  zeichnen();
}

// Seite verlassen (anderswohin): Anlernen beenden
inline void verlassen() {
  auto &s = z();
  if (s.ansicht == LERNEN && !s.eingabe && s.lern.phase != 3) {
    lernstand("Abgebrochen");
    lern_ende();
    s.ansicht = LISTE;
  }
}

// Alle 100 ms: neue Belegung von HA -> vorlaeufige Aenderungen verwerfen, Seite neu zeichnen
inline void takt() {
  auto &s = z();
  // Live-Anzeige beim Anlernen: wann die Remote bereit ist
  if (s.ansicht == LERNEN && s.lern.phase == 0 && cur_page->value() == 45 && s.lbl_klein) {
    const auto &g = gesten::z();
    const int b = g.in_bewegung ? 2 : (g.ruhig >= 15 && millis() - last_input_ms->value() > 700) ? 1 : 0;
    if (b != s.bereit) {
      s.bereit = b;
      static const char *T[] = {"Ruhig halten ...", "Jetzt die Geste ausführen", "Bewegung läuft ..."};
      static const uint32_t F[] = {0xFACC15, 0x22C55E, 0x3B82F6};
      lv_label_set_text(s.lbl_klein, T[b]);
      lv_obj_set_style_text_color(s.lbl_klein, lv_color_hex(F[b]), 0);
    }
  }
  if (s.gen == tk_cfg->generation()) return;
  s.gen = tk_cfg->generation();
  s.an_neu.clear(); s.taste_neu.clear(); s.name_neu.clear(); s.weg.clear();
  if (cur_page->value() == 45 && !(s.ansicht == LERNEN && s.lern.phase < 3)) zeichnen();
}

// ---------------------------------------------------------------- Namenseingabe (Bildschirmtastatur, Seite 41)
inline void kbd_titel() {
  auto &s = z();
  if (!s.eingabe) return;
  std::string t = "Name: " + s.text + "_";
  lv_label_set_text(kbd_title, t.c_str());
}

// Laeuft gerade die Namenseingabe auf der Tastatur? (Tastatur-Seite blendet dann den Eingabe-Umschalter aus)
inline bool namen_eingabe() { return z().eingabe; }

// true = Taste gehoert der Namenseingabe
inline bool kbd_taste(const std::string &k) {
  auto &s = z();
  if (!s.eingabe || cur_page->value() != 41 || k == "^") return false;
  if (k == "<–") {
    while (!s.text.empty()) {   // UTF-8: ganzes Zeichen entfernen
      const unsigned char c = s.text.back();
      s.text.pop_back();
      if ((c & 0xC0) != 0x80) break;
    }
  } else if (k == "Ent" || k == "OK") {
    if (s.text.empty()) return true;
    s.eingabe = false;
    if (s.eingabe_zweck == 1) {
      lern_starten(neue_id(), s.text);
      return true;
    }
    s.name_neu[s.sel] = s.text;
    aendern("name", s.sel, s.text);
    s.behalten = true;
    s.ansicht = GESTE;
    tasten::seite_zeigen(page_gesten, LV_SCR_LOAD_ANIM_NONE);
    return true;
  } else if (k == "Leer") {
    if (s.text.size() < 24) s.text += ' ';
  } else if (s.text.size() + k.size() <= 24) {
    s.text += k;
    if (kbd_shift->value()) { kbd_shift->value() = false; kbd_setup->execute(); }
  }
  kbd_titel();
  return true;
}

// Zurueck auf der Tastatur waehrend der Namenseingabe: zurueck zur Gesten-Seite
inline bool kbd_abbruch() {
  auto &s = z();
  if (!s.eingabe) return false;
  s.eingabe = false;
  s.behalten = true;
  tasten::seite_zeigen(page_gesten, LV_SCR_LOAD_ANIM_NONE);
  return true;
}

}  // namespace gestenseite
